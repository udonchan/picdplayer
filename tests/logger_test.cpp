#include "logger.hpp"

#include <condition_variable>
#include <future>
#include <mutex>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string>

// Hold the sink outside the producer queue lock; simulate a stalled pipe.
// 出力先だけを停止し、submit側の進行とqueue上限を確認する。
struct StalledSink : std::streambuf {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    int overflow(int c) override {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
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

int main() {
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

    bool rejected = false;
    try { AsyncLogger invalid(normal, errors, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
}
