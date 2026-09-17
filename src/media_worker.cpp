#include "media_worker.hpp"
#include <stdexcept>
#include <utility>

MediaWorker::MediaWorker(Observe observe, ReadToc read_toc, Eject eject, ProbeDrive probe_drive)
    : observe_(std::move(observe)), read_toc_(std::move(read_toc)), eject_(std::move(eject)),
      probe_drive_(std::move(probe_drive)) {
    if (!observe_ || !read_toc_ || !eject_) throw std::invalid_argument("media worker callback is empty");
    thread_ = std::thread(&MediaWorker::run, this);
}

MediaWorker::~MediaWorker() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
    }
    changed_.notify_all();
    thread_.join();
}

bool MediaWorker::request(MediaWork work) {
    std::lock_guard lock(mutex_);
    if (closing_ || busy_ || pending_ || !results_.empty()) return false;
    pending_ = work;
    changed_.notify_all();
    return true;
}

bool MediaWorker::pop(MediaWorkerResult& result) {
    std::lock_guard lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

void MediaWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || pending_.has_value(); });
        if (closing_) return;
        const auto work = *pending_;
        pending_.reset();
        busy_ = true;
        lock.unlock();

        MediaWorkerResult result{work, std::nullopt, std::nullopt, std::nullopt, {}};
        try {
            if (work == MediaWork::probe_drive) {
                if (!probe_drive_) throw std::runtime_error("drive probe is unavailable");
                result.drive = probe_drive_();
            } else if (work == MediaWork::observe) result.observation = observe_();
            else if (work == MediaWork::read_toc) result.toc = read_toc_();
            else eject_();
        } catch (const std::exception& error) {
            result.error = error.what();
        }

        lock.lock();
        busy_ = false;
        if (!closing_) results_.push_back(std::move(result));
    }
}
