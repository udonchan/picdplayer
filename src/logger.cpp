#include "logger.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
const char* level_name(LogLevel level) {
    switch (level) {
    case LogLevel::debug: return "DEBUG";
    case LogLevel::info: return "INFO";
    case LogLevel::warning: return "WARN";
    case LogLevel::error: return "ERROR";
    }
    return "UNKNOWN";
}

bool low_priority(LogLevel level) {
    return level == LogLevel::debug || level == LogLevel::info;
}

std::size_t checked_capacity(std::size_t capacity) {
    if (capacity == 0) throw std::invalid_argument("logger capacity must be positive");
    return capacity;
}
}

AsyncLogger::AsyncLogger(std::ostream& normal_sink, std::ostream& error_sink,
                         std::size_t capacity)
    : normal_sink_(normal_sink), error_sink_(error_sink), capacity_(checked_capacity(capacity)),
      started_(std::chrono::steady_clock::now()), thread_([this] { run(); }) {}

AsyncLogger::~AsyncLogger() { shutdown(); }

void AsyncLogger::submit(LogLevel level, std::string component, std::string message) noexcept {
    try {
        Record record{std::chrono::system_clock::now(), std::chrono::steady_clock::now(),
                      level, std::move(component), std::move(message)};
        {
            std::lock_guard lock(mutex_);
            if (stopping_) {
                ++dropped_;
                return;
            }
            if (queue_.size() >= capacity_) {
                if (!low_priority(level)) {
                    const auto candidate = std::find_if(queue_.begin(), queue_.end(),
                        [](const Record& queued) { return low_priority(queued.level); });
                    if (candidate != queue_.end()) queue_.erase(candidate);
                    else {
                        ++dropped_;
                        return;
                    }
                } else {
                    ++dropped_;
                    return;
                }
                ++dropped_;
            }
            queue_.push_back(std::move(record));
        }
        ready_.notify_one();
    } catch (...) {
        ++dropped_;
    }
}

void AsyncLogger::shutdown() noexcept {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    ready_.notify_one();
    if (thread_.joinable()) thread_.join();
}

std::uint64_t AsyncLogger::dropped() const noexcept { return dropped_.load(); }

void AsyncLogger::run() noexcept {
    for (;;) {
        Record record;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stopping_) break;
                continue;
            }
            record = std::move(queue_.front());
            queue_.pop_front();
        }
        write(record);
    }
    const auto lost = dropped_.load();
    if (lost != 0) {
        Record record{std::chrono::system_clock::now(), std::chrono::steady_clock::now(),
                      LogLevel::warning, "logger", "dropped=" + std::to_string(lost)};
        write(record);
    }
}

void AsyncLogger::write(const Record& record) noexcept {
    try {
        const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.wall_time.time_since_epoch());
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(wall_ms);
        const auto milliseconds = wall_ms - seconds;
        const std::time_t time = std::chrono::system_clock::to_time_t(record.wall_time);
        std::tm utc{};
        gmtime_r(&time, &utc);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.monotonic_time - started_).count();
        auto& sink = low_priority(record.level) ? normal_sink_ : error_sink_;
        sink << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
             << std::setfill('0') << std::setw(3) << milliseconds.count()
             << "Z +" << elapsed_ms << "ms " << level_name(record.level) << ' '
             << record.component << ": " << record.message << '\n' << std::flush;
    } catch (...) {
        // Logging must not terminate the daemon.
    }
}

LogLine::LogLine(LogLevel level, std::string_view component)
    : level_(level), component_(component) {}

LogLine::LogLine(LogLine&& other) noexcept
    : level_(other.level_), component_(std::move(other.component_)),
      stream_(std::move(other.stream_)), active_(other.active_) {
    other.active_ = false;
}

LogLine::~LogLine() noexcept {
    if (!active_) return;
    try { global_logger().submit(level_, std::move(component_), stream_.str()); }
    catch (...) {}
}

AsyncLogger& global_logger() {
    static AsyncLogger logger(std::cout, std::cerr);
    return logger;
}

void shutdown_logger() noexcept { global_logger().shutdown(); }
LogLine log_debug(std::string_view component) { return {LogLevel::debug, component}; }
LogLine log_info(std::string_view component) { return {LogLevel::info, component}; }
LogLine log_warning(std::string_view component) { return {LogLevel::warning, component}; }
LogLine log_error(std::string_view component) { return {LogLevel::error, component}; }
