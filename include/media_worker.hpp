#pragma once

#include "disc_toc.hpp"
#include "media_state.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

enum class MediaWork { observe, read_toc };

struct MediaWorkerResult {
    MediaWork work;
    std::optional<MediaObservation> observation;
    std::optional<DiscToc> toc;
    std::string error;
};

// Serializes potentially blocking status and TOC calls off the main thread.
class MediaWorker {
public:
    using Observe = std::function<MediaObservation()>;
    using ReadToc = std::function<DiscToc()>;

    MediaWorker(Observe observe, ReadToc read_toc);
    ~MediaWorker();
    MediaWorker(const MediaWorker&) = delete;
    MediaWorker& operator=(const MediaWorker&) = delete;

    bool request(MediaWork work);
    bool pop(MediaWorkerResult& result);

private:
    void run();
    Observe observe_;
    ReadToc read_toc_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<MediaWorkerResult> results_;
    std::optional<MediaWork> pending_;
    bool busy_ = false;
    bool closing_ = false;
    std::thread thread_;
};
