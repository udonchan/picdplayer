#pragma once
#include "integrity_state.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct DiagnosticContext {
    std::uint64_t stream_generation = 0;
    std::uint64_t disc_generation = 0; // zero means no validated disc
    bool operator==(const DiagnosticContext&) const = default;
};
struct DiagnosticPublication {
    std::string map;
    std::string history;
};
struct DiagnosticCapture {
    ReadDiagnostics read;
    std::uint64_t sequence = 0;
    std::int64_t captured_at_unix_ms = 0;
    bool detail = false;
};
DiagnosticPublication serialize_read_diagnostics(const DiagnosticCapture& capture);

// The main owner only captures bounded facts. JSON construction and destruction
// stay on this worker; requests share one active and one replaceable pending job.
class ReadDiagnosticsPublisher {
public:
    using Capture = std::function<ReadDiagnostics(bool)>;
    using Serialize = std::function<DiagnosticPublication(const DiagnosticCapture&)>;
    explicit ReadDiagnosticsPublisher(Serialize serialize = {},
        std::chrono::milliseconds refresh = std::chrono::milliseconds(2000));
    ~ReadDiagnosticsPublisher();
    ReadDiagnosticsPublisher(const ReadDiagnosticsPublisher&) = delete;
    ReadDiagnosticsPublisher& operator=(const ReadDiagnosticsPublisher&) = delete;
    // Main-owner-only. Empty means not ready; never wait for JSON generation.
    // Capture must only copy bounded in-memory facts, not perform I/O or JSON work.
    std::string request(DiagnosticContext context, bool detail, const Capture& capture);
private:
    void run();
    Serialize serialize_;
    std::chrono::milliseconds refresh_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool closing_ = false, active_ = false;
    DiagnosticContext context_{}, active_context_{};
    std::uint64_t context_revision_ = 0, active_revision_ = 0;
    bool active_detail_ = false;
    std::optional<DiagnosticCapture> pending_;
    std::shared_ptr<const std::string> map_, history_;
    std::uint64_t sequence_ = 0, map_sequence_ = 0, history_sequence_ = 0;
    std::optional<std::chrono::steady_clock::time_point> map_requested_, history_requested_;
    std::thread thread_;
};
