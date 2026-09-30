#pragma once
#include "disc_toc.hpp"
#include "metadata_model.hpp"
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

struct MetadataRequest { std::uint64_t generation; DiscToc toc; };
struct MetadataWorkerResult { std::uint64_t generation; DiscToc toc; MetadataResult metadata; };

class MetadataWorker {
public:
    using Cancelled = std::function<bool()>;
    using Lookup = std::function<MetadataResult(const DiscToc&, const Cancelled&)>;
    explicit MetadataWorker(Lookup lookup);
    ~MetadataWorker();
    MetadataWorker(const MetadataWorker&) = delete;
    MetadataWorker& operator=(const MetadataWorker&) = delete;
    void request(MetadataRequest request);
    bool pop(MetadataWorkerResult& result);
    void cancel_pending();
private:
    void run();
    Lookup lookup_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<MetadataRequest> pending_;
    std::optional<MetadataWorkerResult> result_;
    std::atomic<bool> closing_ = false;
    std::thread thread_;
};

struct ArtworkRequest { std::uint64_t generation; std::string release_id; };
struct ArtworkWorkerResult { ArtworkRequest request; ArtworkInfo artwork; };

// A separate queue keeps manual artwork lookup off the playback event loop.
class ArtworkWorker {
public:
    using Lookup = std::function<ArtworkInfo(const std::string&, const MetadataWorker::Cancelled&)>;
    explicit ArtworkWorker(Lookup lookup);
    ~ArtworkWorker();
    ArtworkWorker(const ArtworkWorker&) = delete;
    ArtworkWorker& operator=(const ArtworkWorker&) = delete;
    void request(ArtworkRequest request);
    bool pop(ArtworkWorkerResult& result);
    void cancel_pending();
private:
    void run();
    Lookup lookup_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<ArtworkRequest> pending_;
    std::optional<ArtworkWorkerResult> result_;
    std::atomic<std::uint64_t> request_serial_ = 0;
    std::atomic<bool> closing_ = false;
    std::thread thread_;
};
