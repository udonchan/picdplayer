#pragma once

#include "api_server.hpp"
#include "complete_pcm_session.hpp"
#include "media_worker.hpp"

struct C2AssessmentContext {
    CompletePcmContext input;
    bool direct = false;
    bool c2_supported = false;
    bool capture_idle = false;
};

// Main-thread owner. The MediaWorker callback owns all raw I/O and retirement.
class C2AssessmentSession {
public:
    C2AssessmentSession(std::string session_id, MediaWorker& worker);
    const char* start_rejection(const C2AssessmentContext&) const;
    ApiCommandResult start(const ApiCommand&, const C2AssessmentContext&);
    ApiCommandResult cancel(const ApiCommand&);
    bool interrupt();
    void offer(const MediaWorkerResult&);
    void poll(const C2AssessmentContext&);
    bool busy() const noexcept { return active_; }
    bool released() const noexcept { return released_; }
    CapabilityFlag flag(const C2AssessmentContext&) const;
    ApiResponse response(const C2AssessmentContext&) const;
private:
    bool context_current(const C2AssessmentContext&) const;
    std::string session_id_;
    MediaWorker& worker_;
    std::uint64_t next_job_ = 0, job_ = 0, disc_ = 0, observation_ = 0, policy_ = 0;
    std::optional<C2Reference> reference_;
    std::optional<MediaWorkerResult> staged_;
    CapabilityFlag flag_;
    C2AssessmentCounts counts_;
    std::string state_ = "IDLE", error_;
    bool active_ = false, released_ = true;
};
