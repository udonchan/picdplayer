#pragma once
#include "api_server.hpp"
#include "complete_pcm_worker.hpp"

// Values observed by the session owner, never inferred from presentation JSON.
struct CompletePcmContext {
    CompletePcmStartGate gate;
    const DiscToc* toc = nullptr;
    bool device_current = false, media_idle = false, policy_pending = false;
    std::uint64_t disc_generation = 0, observation_generation = 0, policy_revision = 0;
    std::optional<std::int32_t> offset_samples;
    ReadPolicy policy;
};

// Main-thread owner. No I/O until a successful explicit start. Worker teardown
// joins only at session shutdown, not during cancellation/device retirement.
class CompletePcmSession {
public:
    CompletePcmSession(std::string session_id, std::string backend,
        CompletePcmWorker::Factory factory, CompletePcmWorker::TocProbe probe,
        std::shared_ptr<DriveAccessCoordinator> access);
    const char* start_rejection(const CompletePcmContext&) const;
    ApiCommandResult start(const ApiCommand&, const CompletePcmContext&);
    ApiCommandResult cancel(const ApiCommand&);
    // Invalidates even an already confirmed receipt; true means release pending.
    bool interrupt();
    void poll(const CompletePcmContext&);
    bool busy() const { return status_.busy; }
    bool released() const { return status_.reader_released; }
    ApiResponse response(const CompletePcmContext&, bool result) const;
private:
    CompletePcmIdentity identity(const CompletePcmContext&) const;
    bool current(const CompletePcmContext&) const;
    std::string session_id_, backend_, manifest_;
    CompletePcmWorker worker_;
    CompletePcmWorkerStatus status_;
    std::optional<CompletePcmPlan> plan_;
    std::uint64_t next_job_ = 0;
};
