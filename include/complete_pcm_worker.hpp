#pragma once

#include "complete_pcm_input.hpp"
#include "drive_access.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

// Assertions supplied by the owner; this type does not sample ALSA or media.
struct CompletePcmStartGate {
    bool playback_stopped = false;
    bool output_stopped = false;
    bool queue_cancelled = false;
    bool playback_reader_released = false;
    bool audio_ready = false;
    bool ready() const {
        return playback_stopped && output_stopped && queue_cancelled && playback_reader_released && audio_ready;
    }
};

enum class CompletePcmWorkerPhase {
    idle, queued, initial_toc, opening, seeking, reading, finalizing, final_toc,
    releasing, prepared, finished
};
enum class CompletePcmWorkerFailure {
    none, cancelled, context_changed, not_ready, job_deadline, call_deadline,
    clock_regressed, clock_failed, operation_failed, core_failed, toc_changed, memory_limit
};
struct CompletePcmWorkerStatus {
    std::optional<CompletePcmIdentity> identity;
    std::optional<CompletePcmState> input_state;
    CompletePcmWorkerPhase phase = CompletePcmWorkerPhase::idle;
    CompletePcmWorkerFailure failure = CompletePcmWorkerFailure::none;
    CompletePcmFailure core_failure = CompletePcmFailure::none;
    bool busy = false;
    bool reader_released = true;
    bool waiting_for_access = false;
    bool call_inflight = false;
    std::uint64_t accepted_frames = 0, accepted_bytes = 0;
    IntegrityStats read_observations;
    std::uint64_t uncertain_reads = 0, recovered_reads = 0;
    std::size_t reserved_bytes = 0, pcm_capacity_bytes = 0, peak_pcm_bytes = 0;
    int native_error = 0;
};

// Internal, explicit-use worker. No production factory or runtime registration.
// Reader calls/destruction and accepted consumer processing belong to this
// thread. cancel/poll/confirm never wait for device/consumer work. start has
// bounded caller-side CPU preflight; destruction joins and may wait.
class CompletePcmWorker {
public:
    using Factory = std::function<std::unique_ptr<CddaReader>()>;
    using TocProbe = std::function<DiscToc()>;
    // Factory returns a fresh raw reader, not a pre-opened/shared instance.
    // Factory/probe must not re-enter access. The clock is shared by control
    // and worker code and must be thread-safe, nonblocking and monotonic.
    CompletePcmWorker(Factory raw_factory, TocProbe probe,
                      std::shared_ptr<DriveAccessCoordinator> access,
                      SteadyNow now = [] { return std::chrono::steady_clock::now(); });
    ~CompletePcmWorker();
    CompletePcmWorker(const CompletePcmWorker&) = delete;
    CompletePcmWorker& operator=(const CompletePcmWorker&) = delete;
    // Busy/not-ready/stale admission returns false; other plan validation errors
    // throw before I/O. Job IDs must increase over this worker's lifetime.
    bool start(const CompletePcmPlan& plan, std::unique_ptr<CompletePcmConsumer> consumer,
               const CompletePcmStartGate& gate);
    void cancel();
    CompletePcmWorkerStatus poll(const CompletePcmIdentity& current, const CompletePcmStartGate& gate);
    bool confirm(const CompletePcmIdentity& current, const CompletePcmStartGate& gate);
    std::vector<std::uint8_t> result(const CompletePcmIdentity& current) const;
private:
    struct Ticket { std::uint64_t value = 0; };
    struct Job {
        CompletePcmPlan plan;
        std::shared_ptr<Ticket> ticket;
        std::unique_ptr<CompletePcmInput> core;
    };
    void run();
    void execute(Job& job);
    bool permitted(std::uint64_t ticket);
    bool permitted_locked(std::uint64_t ticket);
    bool call(Job& job, CompletePcmWorkerPhase phase, const std::function<void()>& action, bool cleanup = false);
    void refresh_locked();
    void update_locked(const CompletePcmIdentity& current, const CompletePcmStartGate& gate);
    void stop_locked(CompletePcmState state, CompletePcmWorkerFailure reason);
    void publish_core(Job& job);
    Factory factory_;
    TocProbe probe_;
    std::shared_ptr<DriveAccessCoordinator> access_;
    SteadyNow now_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool closing_ = false;
    std::uint64_t epoch_ = 0, active_ticket_ = 0, last_job_id_ = 0;
    CompletePcmWorkerStatus status_;
    std::chrono::steady_clock::time_point last_now_, job_deadline_;
    std::optional<std::chrono::steady_clock::time_point> call_deadline_;
    unsigned call_budget_ms_ = 0;
    std::unique_ptr<Job> queued_;
    std::vector<std::uint8_t> pending_result_, result_;
    std::thread thread_;
};
