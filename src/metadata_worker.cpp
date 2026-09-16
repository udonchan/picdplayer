#include "metadata_worker.hpp"
#include <stdexcept>
#include <utility>

MetadataWorker::MetadataWorker(Lookup lookup) : lookup_(std::move(lookup)), thread_(&MetadataWorker::run, this) {
    if (!lookup_) throw std::invalid_argument("metadata worker callback is empty");
}
MetadataWorker::~MetadataWorker() { { std::lock_guard lock(mutex_); closing_ = true; pending_.reset(); } changed_.notify_all(); thread_.join(); }
void MetadataWorker::request(MetadataRequest request) { std::lock_guard lock(mutex_); if (!closing_) { pending_ = std::move(request); result_.reset(); changed_.notify_all(); } }
bool MetadataWorker::pop(MetadataWorkerResult& result) { std::lock_guard lock(mutex_); if (!result_) return false; result = std::move(*result_); result_.reset(); return true; }
void MetadataWorker::cancel_pending() { std::lock_guard lock(mutex_); pending_.reset(); result_.reset(); }
void MetadataWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_); changed_.wait(lock, [&] { return closing_ || pending_; });
        if (closing_) return;
        auto request = std::move(*pending_); pending_.reset(); lock.unlock();
        MetadataWorkerResult result{request.generation, request.toc, {}};
        try { result.metadata = lookup_(request.toc); }
        catch (const std::exception& e) { result.metadata.status = MetadataStatus::error; result.metadata.error = e.what(); }
        lock.lock(); if (!closing_) result_ = std::move(result);
    }
}
