#include "logger.hpp"

#include <condition_variable>
#include <future>
#include <mutex>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdlib>

// Hold the sink outside the producer queue lock; simulate a stalled pipe.
// 出力先だけを停止し、submit側の進行とqueue上限を確認する。
struct StalledSink : std::streambuf {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    std::string text;
    int overflow(int c) override {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
        text.push_back(static_cast<char>(c));
        return c;
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
struct FailedSink : std::streambuf {
    int overflow(int) override { return traits_type::eof(); }
};
void require(bool ok) { if (!ok) throw std::runtime_error("logger fault test failed"); }

struct CountFormatting {
    int& count;
};
std::ostream& operator<<(std::ostream& stream, CountFormatting value) {
    ++value.count;
    return stream << "formatted";
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--profile") {
        log_debug("profile") << "debug message";
        log_info("profile") << "info message";
        log_notice("profile") << "notice message";
        log_warning("profile") << "warning message";
        log_error("profile") << "error message";
        shutdown_logger();
        return 0;
    }
    // Establish the process-wide filter before its first use.
    setenv("PICDPLAYER_LOG_LEVEL", "warning", 1);
    int formatted = 0;
    log_debug("test") << CountFormatting{formatted};
    log_info("test") << CountFormatting{formatted};
    require(formatted == 0);
    require(log_enabled(LogLevel::notice) && log_enabled(LogLevel::warning) && log_enabled(LogLevel::error));
    for (auto minimum : {LogLevel::debug, LogLevel::info, LogLevel::warning}) {
        std::ostringstream normal, errors;
        AsyncLogger filtered(normal, errors, 8, minimum);
        filtered.submit(LogLevel::debug, "test", "debug message");
        filtered.submit(LogLevel::info, "test", "info message");
        filtered.submit(LogLevel::notice, "test", "identity");
        filtered.submit(LogLevel::warning, "test", "warning message");
        filtered.submit(LogLevel::error, "test", "error message");
        filtered.shutdown();
        require((normal.str().find("DEBUG") != std::string::npos) == (minimum == LogLevel::debug));
        require((normal.str().find("INFO") != std::string::npos) == (minimum != LogLevel::warning));
        require(normal.str().find("NOTICE test: identity") != std::string::npos);
        require(errors.str().find("WARN test: warning message") != std::string::npos);
        require(errors.str().find("ERROR test: error message") != std::string::npos);
        require(filtered.dropped() == 0);
    }
    std::ostringstream normal;
    std::ostringstream errors;
    {
        AsyncLogger logger(normal, errors, 8);
        logger.submit(LogLevel::info, "player", "state=PLAYING");
        logger.submit(LogLevel::warning, "player", "main_loop_stall stage=test");
    }
    const auto info = normal.str();
    const auto warning = errors.str();
    require(info.find("Z +") != std::string::npos);
    require(info.find(" INFO player: state=PLAYING") != std::string::npos);
    require(warning.find(" WARN player: main_loop_stall stage=test") != std::string::npos);

    {
        StalledSink buffer;
        std::ostream sink(&buffer);
        AsyncLogger logger(sink, sink, 8);
        logger.submit(LogLevel::info, "test", "block sink");
        bool entered;
        {
            std::unique_lock lock(buffer.mutex);
            entered = buffer.changed.wait_for(lock, std::chrono::seconds(2), [&] { return buffer.entered; });
        }
        if (!entered) { buffer.release(); throw std::runtime_error("sink did not enter"); }
        auto producer = std::async(std::launch::async, [&] {
            for (int i = 0; i < 100; ++i) logger.submit(LogLevel::info, "test", "queued");
        });
        const bool progressed = producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        const auto dropped = logger.dropped();
        buffer.release(); // Always release before joining, including assertion failures.
        producer.get();
        logger.shutdown();
        require(progressed && dropped == 92);
    }
    {
        FailedSink buffer;
        std::ostream sink(&buffer);
        sink.exceptions(std::ios::badbit | std::ios::failbit);
        AsyncLogger logger(sink, sink, 8);
        logger.submit(LogLevel::error, "test", "write failure");
        logger.shutdown();
        require(sink.bad()); // Prove the injected write actually failed.
    }

    for (auto level : {LogLevel::notice, LogLevel::warning, LogLevel::error}) {
        StalledSink buffer;
        std::ostream sink(&buffer);
        AsyncLogger logger(sink, sink, 2);
        logger.submit(LogLevel::info, "test", "block sink");
        bool entered;
        {
            std::unique_lock lock(buffer.mutex);
            entered = buffer.changed.wait_for(lock, std::chrono::seconds(2), [&] { return buffer.entered; });
        }
        if (!entered) { buffer.release(); throw std::runtime_error("sink did not enter"); }
        logger.submit(LogLevel::debug, "test", "evict this record");
        logger.submit(LogLevel::info, "test", "retain queued record");
        logger.submit(level, "test", "priority preserved");
        buffer.release();
        logger.shutdown();
        require(logger.dropped() == 1);
        require(buffer.text.find("priority preserved") != std::string::npos);
        require(buffer.text.find("evict this record") == std::string::npos);
    }

    bool rejected = false;
    try { AsyncLogger invalid(normal, errors, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
}
