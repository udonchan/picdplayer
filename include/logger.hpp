#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

enum class LogLevel { debug, info, warning, error };

class AsyncLogger {
public:
    AsyncLogger(std::ostream& normal_sink, std::ostream& error_sink,
                std::size_t capacity = 512);
    ~AsyncLogger();
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    void submit(LogLevel level, std::string component, std::string message) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
    struct Record {
        std::chrono::system_clock::time_point wall_time;
        std::chrono::steady_clock::time_point monotonic_time;
        LogLevel level;
        std::string component;
        std::string message;
    };

    void run() noexcept;
    void write(const Record& record) noexcept;

    std::ostream& normal_sink_;
    std::ostream& error_sink_;
    const std::size_t capacity_;
    const std::chrono::steady_clock::time_point started_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Record> queue_;
    bool stopping_ = false;
    std::atomic<std::uint64_t> dropped_{0};
    std::thread thread_;
};

class LogLine {
public:
    LogLine(LogLevel level, std::string_view component);
    ~LogLine() noexcept;
    LogLine(LogLine&& other) noexcept;
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    template <typename T>
    LogLine& operator<<(T&& value) {
        stream_ << std::forward<T>(value);
        return *this;
    }

private:
    LogLevel level_;
    std::string component_;
    std::ostringstream stream_;
    bool active_ = true;
};

AsyncLogger& global_logger();
void shutdown_logger() noexcept;
LogLine log_debug(std::string_view component);
LogLine log_info(std::string_view component);
LogLine log_warning(std::string_view component);
LogLine log_error(std::string_view component);
