#pragma once

#include "disc_toc.hpp"
#include "drive_capabilities.hpp"
#include "media_state.hpp"
#include "drive_access.hpp"
#include "c2_assessment_run.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

enum class MediaWork { probe_drive, observe, read_toc, start_drive, stop_drive, set_drive_speed, eject, assess_c2 };

struct MediaWorkerResult {
    MediaWork work;
    std::optional<MediaObservation> observation;
    std::optional<DiscToc> toc;
    std::optional<DriveCapabilities> drive;
    std::string error;
    std::uint64_t observation_generation = 0;
    std::optional<C2AssessmentRunResult> c2_assessment{};
    std::uint64_t c2_job_id = 0;
};

// Serializes potentially blocking status and TOC calls off the main thread.
class MediaWorker {
public:
    using Observe = std::function<MediaObservation()>;
    using ReadToc = std::function<DiscToc()>;
    using Eject = std::function<void()>;
    using StartDrive = std::function<void()>;
    using StopDrive = std::function<void()>;
    using SetDriveSpeed = std::function<void()>;
    using ProbeDrive = std::function<DriveCapabilities()>;
    using AssessC2 = std::function<C2AssessmentRunResult(const C2Reference&, const std::function<bool()>&)>;

    using GenerationCurrent = std::function<bool(std::uint64_t)>;
    // Callbacks are raw operations: this owner acquires access and checks the
    // generation after acquiring it. Callbacks must not re-enter access.
    MediaWorker(std::shared_ptr<DriveAccessCoordinator> access,
                Observe observe, ReadToc read_toc, Eject eject, ProbeDrive probe_drive = {},
                StartDrive start_drive = {}, SetDriveSpeed set_drive_speed = {}, StopDrive stop_drive = {},
                GenerationCurrent current = {}, AssessC2 assess_c2 = {});
    ~MediaWorker();
    MediaWorker(const MediaWorker&) = delete;
    MediaWorker& operator=(const MediaWorker&) = delete;

    bool request(MediaWork work);
    bool request_c2(C2Reference reference, std::uint64_t job_id);
    void cancel_c2();
    bool pop(MediaWorkerResult& result);
    void invalidate(std::uint64_t observation_generation);
    bool idle() const;
    std::string_view activity() const;
    bool retiring() const;

private:
    void run();
    bool current(std::uint64_t generation) const;
    std::shared_ptr<DriveAccessCoordinator> access_;
    GenerationCurrent current_;
    Observe observe_;
    ReadToc read_toc_;
    Eject eject_;
    ProbeDrive probe_drive_;
    StartDrive start_drive_;
    StopDrive stop_drive_;
    SetDriveSpeed set_drive_speed_;
    AssessC2 assess_c2_;
    std::optional<C2Reference> pending_c2_;
    std::uint64_t pending_c2_job_ = 0;
    bool c2_cancelled_ = false;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<MediaWorkerResult> results_;
    std::optional<MediaWork> pending_;
    bool busy_ = false;
    bool closing_ = false;
    std::uint64_t observation_generation_ = 0;
    std::uint64_t running_generation_ = 0;
    std::thread thread_;
};
