#include "media_worker.hpp"
#include <stdexcept>
#include <utility>

MediaWorker::MediaWorker(std::shared_ptr<DriveAccessCoordinator> access,
                         Observe observe, ReadToc read_toc, Eject eject,
                         ProbeDrive probe_drive, StartDrive start_drive,
                         SetDriveSpeed set_drive_speed, StopDrive stop_drive, GenerationCurrent current, AssessC2 assess_c2)
    : access_(std::move(access)), current_(std::move(current)),
      observe_(std::move(observe)), read_toc_(std::move(read_toc)), eject_(std::move(eject)),
      probe_drive_(std::move(probe_drive)), start_drive_(std::move(start_drive)),
      stop_drive_(std::move(stop_drive)), set_drive_speed_(std::move(set_drive_speed)), assess_c2_(std::move(assess_c2)) {
    if (!access_ || !observe_ || !read_toc_ || !eject_) throw std::invalid_argument("media worker callback is empty");
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
    if (work == MediaWork::assess_c2 || closing_ || busy_ || pending_ || !results_.empty()) return false;
    pending_ = work;
    changed_.notify_all();
    return true;
}

bool MediaWorker::request_c2(C2Reference reference, std::uint64_t job_id) {
    std::lock_guard lock(mutex_);
    if (!job_id || !assess_c2_ || closing_ || busy_ || pending_ || !results_.empty()) return false;
    pending_c2_ = std::move(reference);
    pending_c2_job_ = job_id;
    c2_cancelled_ = false;
    pending_ = MediaWork::assess_c2;
    changed_.notify_all();
    return true;
}

void MediaWorker::cancel_c2() {
    std::lock_guard lock(mutex_);
    c2_cancelled_ = true;
    if (pending_ == MediaWork::assess_c2) { pending_.reset(); pending_c2_.reset(); }
    for (auto it = results_.begin(); it != results_.end();) {
        if (it->work == MediaWork::assess_c2) it = results_.erase(it);
        else ++it;
    }
}

bool MediaWorker::pop(MediaWorkerResult& result) {
    std::lock_guard lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front());
    results_.pop_front();
    return true;
}

void MediaWorker::invalidate(std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (generation < observation_generation_) throw std::invalid_argument("device observation generation moved backwards");
    if (generation == observation_generation_) return;
    observation_generation_ = generation;
    pending_.reset();
    pending_c2_.reset();
    c2_cancelled_ = true;
    results_.clear();
}

bool MediaWorker::idle() const {
    return activity() == "IDLE";
}

std::string_view MediaWorker::activity() const {
    std::lock_guard lock(mutex_);
    if (busy_) return "RUNNING";
    if (pending_) return "PENDING";
    if (!results_.empty()) return "RESULT_PENDING";
    return "IDLE";
}
bool MediaWorker::retiring() const {
    std::lock_guard lock(mutex_);
    return busy_ && running_generation_ != observation_generation_;
}

bool MediaWorker::current(std::uint64_t generation) const {
    {
        std::lock_guard lock(mutex_);
        if (closing_ || generation != observation_generation_) return false;
    }
    try { return !current_ || current_(generation); }
    catch (...) { return false; }
}

void MediaWorker::run() {
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || pending_.has_value(); });
        if (closing_) return;
        const auto work = *pending_;
        auto reference = std::move(pending_c2_);
        const auto c2_job = pending_c2_job_;
        pending_c2_.reset();
        const auto generation = observation_generation_;
        running_generation_ = generation;
        pending_.reset();
        busy_ = true;
        lock.unlock();

        MediaWorkerResult result{work, std::nullopt, std::nullopt, std::nullopt, {}};
        result.observation_generation = generation;
        result.c2_job_id = work == MediaWork::assess_c2 ? c2_job : 0;
        bool executed = false;
        try {
            if (current(generation)) access_->invoke([&] {
                if (!current(generation)) return;
                if (work == MediaWork::assess_c2) {
                    std::lock_guard guard(mutex_);
                    if (c2_cancelled_) return;
                }
                executed = true;
                if (work == MediaWork::probe_drive) {
                    if (!probe_drive_) throw std::runtime_error("drive probe is unavailable");
                    result.drive = probe_drive_();
                } else if (work == MediaWork::assess_c2) {
                    if (!reference || !assess_c2_) throw std::runtime_error("C2 assessment unavailable");
                    result.c2_assessment = assess_c2_(*reference, [this, generation] {
                        if (!current(generation)) return false;
                        std::lock_guard guard(mutex_);
                        return !c2_cancelled_;
                    });
                } else if (work == MediaWork::observe) result.observation = observe_();
                else if (work == MediaWork::read_toc) result.toc = read_toc_();
                else if (work == MediaWork::start_drive) {
                    if (!start_drive_) throw std::runtime_error("drive start is unavailable");
                    start_drive_();
                } else if (work == MediaWork::stop_drive) {
                    if (!stop_drive_) throw std::runtime_error("drive stop is unavailable");
                    stop_drive_();
                } else if (work == MediaWork::set_drive_speed) {
                    if (!set_drive_speed_) throw std::runtime_error("drive speed control is unavailable");
                    set_drive_speed_();
                } else if (work == MediaWork::eject) eject_();
                else throw std::invalid_argument("unknown media operation");
            });
        } catch (const std::exception& error) {
            result.error = error.what();
        }

        const bool deliver = executed && current(generation);
        lock.lock();
        busy_ = false;
        if (!closing_ && generation == observation_generation_ && deliver &&
            !(work == MediaWork::assess_c2 && c2_cancelled_)) results_.push_back(std::move(result));
    }
}
