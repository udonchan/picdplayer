#include "metadata_worker.hpp"
#include <stdexcept>
#include <utility>

namespace {
MetadataWorker::Lookup require_lookup(MetadataWorker::Lookup lookup) {
    if (!lookup) throw std::invalid_argument("metadata worker callback is empty");
    return lookup;
}
}
MetadataWorker::MetadataWorker(Lookup lookup)
    : lookup_(require_lookup(std::move(lookup))), thread_(&MetadataWorker::run, this) {}
MetadataWorker::~MetadataWorker() { { std::lock_guard lock(mutex_); closing_.store(true); pending_.reset(); } changed_.notify_all(); thread_.join(); }
void MetadataWorker::request(MetadataRequest request) { std::lock_guard lock(mutex_); if (!closing_.load()) { pending_ = std::move(request); result_.reset(); changed_.notify_all(); } }
bool MetadataWorker::pop(MetadataWorkerResult& result) { std::lock_guard lock(mutex_); if (!result_) return false; result = std::move(*result_); result_.reset(); return true; }
void MetadataWorker::cancel_pending() { std::lock_guard lock(mutex_); pending_.reset(); result_.reset(); }
void MetadataWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_); changed_.wait(lock, [&] { return closing_.load() || pending_; });
        if (closing_.load()) return;
        auto request = std::move(*pending_); pending_.reset(); lock.unlock();
        MetadataWorkerResult result{request.generation, request.toc, {}};
        const Cancelled is_cancelled = [this] { return closing_.load(); };
        try { result.metadata = lookup_(request.toc, is_cancelled); }
        catch (const std::exception& e) { result.metadata.status = MetadataStatus::error; result.metadata.error = e.what(); }
        lock.lock(); if (!closing_.load()) result_ = std::move(result);
    }
}
