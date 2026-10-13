#pragma once

#include "artist_background_image.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct ArtistBackgroundImageRequest {
    std::uint64_t generation = 0;
    std::string artist_mbid;
    ArtistBackgroundCandidate candidate;
};

struct ArtistBackgroundImageWorkerResult {
    ArtistBackgroundImageRequest request;
    std::optional<ArtistBackgroundImage> image;
};

// One active task, one latest pending request and one result. The main owner
// must still check generation, artist and candidate before applying a pop.
class ArtistBackgroundImageWorker {
public:
    using Cancelled = std::function<bool()>;
    using Lookup = std::function<std::optional<ArtistBackgroundImage>(
        const ArtistBackgroundImageRequest&, const Cancelled&)>;

    explicit ArtistBackgroundImageWorker(Lookup lookup);
    ~ArtistBackgroundImageWorker();
    ArtistBackgroundImageWorker(const ArtistBackgroundImageWorker&) = delete;
    ArtistBackgroundImageWorker& operator=(const ArtistBackgroundImageWorker&) = delete;
    void request(ArtistBackgroundImageRequest request);
    void cancel_pending();
    bool pop(ArtistBackgroundImageWorkerResult& result);

private:
    void run();
    Lookup lookup_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> closing_ = false;
    std::atomic<std::uint64_t> request_serial_ = 0;
    std::optional<ArtistBackgroundImageRequest> pending_;
    std::optional<ArtistBackgroundImageWorkerResult> result_;
    std::thread thread_;
};
