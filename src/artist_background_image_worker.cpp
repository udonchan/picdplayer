#include "artist_background_image_worker.hpp"
#include <stdexcept>
#include <utility>

namespace {
ArtistBackgroundImageWorker::Lookup require_lookup(ArtistBackgroundImageWorker::Lookup lookup) {
    if (!lookup) throw std::invalid_argument("artist background image worker callback is empty");
    return lookup;
}
}

ArtistBackgroundImageWorker::ArtistBackgroundImageWorker(Lookup lookup)
    : lookup_(require_lookup(std::move(lookup))), thread_([this] { run(); }) {}

ArtistBackgroundImageWorker::~ArtistBackgroundImageWorker() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
        ++request_serial_;
        pending_.reset();
        result_.reset();
    }
    changed_.notify_all();
    thread_.join();
}

void ArtistBackgroundImageWorker::request(ArtistBackgroundImageRequest request) {
    // Match the existing candidate parser's string bounds before queuing a copy.
    if (request.artist_mbid.size() > 36 || request.candidate.id.size() > 64 ||
        request.candidate.url.size() > 4096)
        throw std::invalid_argument("artist background image request exceeds size limit");
    std::lock_guard lock(mutex_);
    if (closing_) return;
    ++request_serial_;
    pending_ = std::move(request);
    result_.reset();
    changed_.notify_all();
}

void ArtistBackgroundImageWorker::cancel_pending() {
    std::lock_guard lock(mutex_);
    ++request_serial_;
    pending_.reset();
    result_.reset();
}

bool ArtistBackgroundImageWorker::pop(ArtistBackgroundImageWorkerResult& result) {
    std::lock_guard lock(mutex_);
    if (!result_) return false;
    result = std::move(*result_);
    result_.reset();
    return true;
}

void ArtistBackgroundImageWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return closing_ || pending_.has_value(); });
        if (closing_) return;
        auto request = std::move(*pending_);
        pending_.reset();
        const auto serial = request_serial_.load();
        lock.unlock();
        const Cancelled cancelled = [this, serial] { return closing_ || request_serial_ != serial; };
        ArtistBackgroundImageWorkerResult output{std::move(request), {}};
        try {
            output.image = lookup_(output.request, cancelled);
            if (output.image && (output.image->bytes.empty() ||
                output.image->bytes.size() > artist_background_image_limit ||
                (output.image->mime_type != "image/jpeg" && output.image->mime_type != "image/png" &&
                 output.image->mime_type != "image/webp"))) output.image.reset();
        } catch (const std::exception&) { output.image.reset(); }
        lock.lock();
        if (!closing_ && request_serial_ == serial) result_ = std::move(output);
    }
}
