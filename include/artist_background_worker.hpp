#pragma once

#include "artist_background_provider.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct ArtistBackgroundRequest {
    std::uint64_t generation = 0;
    std::string artist_mbid;
};

struct ArtistBackgroundWorkerResult {
    ArtistBackgroundRequest request;
    ArtistBackgroundLookup lookup;
};

// A single optional provider task. Replacing or cancelling a request prevents
// the old result from entering the main-thread queue; the caller must still
// compare generation and artist identity before applying any result.
class ArtistBackgroundWorker {
public:
    using Cancelled = std::function<bool()>;
    using Lookup = std::function<ArtistBackgroundLookup(const std::string&, const Cancelled&)>;

    explicit ArtistBackgroundWorker(Lookup lookup);
    ~ArtistBackgroundWorker();
    ArtistBackgroundWorker(const ArtistBackgroundWorker&) = delete;
    ArtistBackgroundWorker& operator=(const ArtistBackgroundWorker&) = delete;

    void request(ArtistBackgroundRequest request);
    void cancel_pending();
    bool pop(ArtistBackgroundWorkerResult& result);

private:
    void run();

    Lookup lookup_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> closing_ = false;
    std::atomic<std::uint64_t> request_serial_ = 0;
    std::optional<ArtistBackgroundRequest> pending_;
    std::optional<ArtistBackgroundWorkerResult> result_;
};
