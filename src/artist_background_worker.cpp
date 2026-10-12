#include "artist_background_worker.hpp"

#include <stdexcept>
#include <utility>

namespace {
ArtistBackgroundWorker::Lookup require_lookup(ArtistBackgroundWorker::Lookup lookup) {
    if (!lookup) throw std::invalid_argument("artist background worker callback is empty");
    return lookup;
}
}

ArtistBackgroundWorker::ArtistBackgroundWorker(Lookup lookup)
    : lookup_(require_lookup(std::move(lookup))), thread_([this] { run(); }) {}

ArtistBackgroundWorker::~ArtistBackgroundWorker() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        ++request_serial_;
        pending_.reset();
    }
    changed_.notify_all();
    thread_.join();
}

void ArtistBackgroundWorker::request(ArtistBackgroundRequest request) {
    std::lock_guard lock(mutex_);
    if (closing_) return;
    ++request_serial_;
    pending_ = std::move(request);
    result_.reset();
    changed_.notify_all();
}

void ArtistBackgroundWorker::cancel_pending() {
    std::lock_guard lock(mutex_);
    ++request_serial_;
    pending_.reset();
    result_.reset();
}

bool ArtistBackgroundWorker::pop(ArtistBackgroundWorkerResult& result) {
    std::lock_guard lock(mutex_);
    if (!result_) return false;
    result = std::move(*result_);
    result_.reset();
    return true;
}

void ArtistBackgroundWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return closing_ || pending_.has_value(); });
        if (closing_) return;
        auto request = std::move(*pending_);
        pending_.reset();
        const auto serial = request_serial_.load();
        lock.unlock();
        const Cancelled cancelled = [this, serial] { return closing_ || request_serial_ != serial; };
        ArtistBackgroundWorkerResult output{request, {}};
        try { output.lookup = lookup_(request.artist_mbid, cancelled); }
        catch (const std::exception&) { output.lookup.status = ArtistBackgroundLookupStatus::error; }
        lock.lock();
        if (!closing_ && request_serial_ == serial) result_ = std::move(output);
    }
}
