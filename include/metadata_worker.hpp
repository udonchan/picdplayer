#pragma once
#include "disc_toc.hpp"
#include "metadata_model.hpp"
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

struct MetadataRequest { std::uint64_t generation; DiscToc toc; };
struct MetadataWorkerResult { std::uint64_t generation; DiscToc toc; MetadataResult metadata; };

class MetadataWorker {
public:
    using Lookup = std::function<MetadataResult(const DiscToc&)>;
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
    bool closing_ = false;
    std::thread thread_;
};
