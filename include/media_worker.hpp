#pragma once

#include "disc_toc.hpp"
#include "drive_capabilities.hpp"
#include "media_state.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

enum class MediaWork { probe_drive, observe, read_toc, eject };

struct MediaWorkerResult {
    MediaWork work;
    std::optional<MediaObservation> observation;
    std::optional<DiscToc> toc;
    std::optional<DriveCapabilities> drive;
    std::string error;
};

// Serializes potentially blocking status and TOC calls off the main thread.
class MediaWorker {
public:
    using Observe = std::function<MediaObservation()>;
    using ReadToc = std::function<DiscToc()>;
    using Eject = std::function<void()>;
    using ProbeDrive = std::function<DriveCapabilities()>;

    MediaWorker(Observe observe, ReadToc read_toc, Eject eject, ProbeDrive probe_drive = {});
    ~MediaWorker();
    MediaWorker(const MediaWorker&) = delete;
    MediaWorker& operator=(const MediaWorker&) = delete;

    bool request(MediaWork work);
    bool pop(MediaWorkerResult& result);

private:
    void run();
    Observe observe_;
    ReadToc read_toc_;
    Eject eject_;
    ProbeDrive probe_drive_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<MediaWorkerResult> results_;
    std::optional<MediaWork> pending_;
    bool busy_ = false;
    bool closing_ = false;
    std::thread thread_;
};
