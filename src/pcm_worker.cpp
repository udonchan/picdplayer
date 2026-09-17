#include "pcm_worker.hpp"
#include <algorithm>
#include <stdexcept>

PcmWorker::PcmWorker(Factory factory, std::string strategy) : factory_(std::move(factory)) {
    if (!factory_) throw std::invalid_argument("reader factory is empty");
    diagnostics_.effective_strategy = std::move(strategy);
    thread_ = std::thread(&PcmWorker::run, this);
}
PcmWorker::~PcmWorker() {
    { std::lock_guard lock(mutex_); closing_ = true; ++generation_; }
    changed_.notify_all();
    thread_.join();
}
std::uint64_t PcmWorker::start(std::int32_t begin, std::int32_t end) {
    if (begin < 0 || end <= begin) throw std::invalid_argument("invalid playback range");
    std::lock_guard lock(mutex_);
    ++generation_;
    begin_ = begin; end_ = end; active_ = true; done_ = false;
    queue_.clear(); error_.clear();
    diagnostics_.activity = ReadActivity::buffering;
    diagnostics_.latest.reset();
    diagnostics_.stats = {};
    events_.clear(); dropped_events_ = 0;
    changed_.notify_all();
    return generation_;
}
void PcmWorker::cancel() {
    std::lock_guard lock(mutex_);
    ++generation_; active_ = false; done_ = false;
    queue_.clear(); error_.clear();
    diagnostics_.activity = ReadActivity::idle;
    diagnostics_.latest.reset();
    events_.clear();
    changed_.notify_all();
}
void PcmWorker::discard_reader() {
    std::lock_guard lock(mutex_);
    ++generation_; active_ = false; done_ = false;
    queue_.clear(); error_.clear();
    diagnostics_.activity = ReadActivity::idle;
    diagnostics_.latest.reset();
    events_.clear();
    discard_reader_ = true;
    changed_.notify_all();
}
bool PcmWorker::device_released() {
    std::lock_guard lock(mutex_);
    return !active_ && !reading_ && !discard_reader_ && !reader_open_;
}
bool PcmWorker::pop(PcmBlock& block) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) return false;
    block = std::move(queue_.front()); queue_.pop_front();
    changed_.notify_all(); return true;
}
bool PcmWorker::pop_event(PlayerEvent& event) {
    std::lock_guard lock(mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.pop_front();
    return true;
}
WorkerStatus PcmWorker::status() {
    std::lock_guard lock(mutex_);
    const auto inflight = reading_ ? std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - read_started_).count() : 0;
    diagnostics_.dropped_events = dropped_events_;
    return {queue_.size(), done_, error_, last_read_us_, inflight, diagnostics_};
}
void PcmWorker::run() {
    std::unique_ptr<CddaReader> reader;
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || active_ || discard_reader_; });
        if (closing_) return;
        if (discard_reader_) {
            discard_reader_ = false;
            lock.unlock();
            reader.reset(); // Never close a device while holding the queue mutex.
            lock.lock();
            reader_open_ = false;
            lock.unlock();
            continue; // Recheck shutdown and any newer Start after slow close.
        }
        const auto generation = generation_;
        auto position = begin_;
        const auto end = end_;
        lock.unlock();
        try {
            if (!reader) {
                reader = factory_();
                lock.lock(); reader_open_ = static_cast<bool>(reader); lock.unlock();
            }
            if (!reader) throw std::runtime_error("reader factory returned null");
            // A slow open may have been superseded by Stop/Seek.
            lock.lock();
            if (closing_) return;
            if (generation != generation_) continue;
            lock.unlock();
            reader->seek(position);
            while (position < end) {
                lock.lock();
                changed_.wait(lock, [&] {
                    return closing_ || generation != generation_ ||
                           queue_.size() < pcm_queue_capacity_blocks;
                });
                if (closing_) return;
                if (generation != generation_) break;
                reading_ = true;
                diagnostics_.activity = ReadActivity::reading;
                read_started_ = std::chrono::steady_clock::now();
                lock.unlock();
                const auto frames = std::min<std::int32_t>(15, end - position);
                PcmBlock block{generation, position,
                               std::vector<std::int16_t>(frames * cdda_samples_per_frame), {}};
                const auto result = reader->read(block.samples);
                lock.lock();
                last_read_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - read_started_).count();
                reading_ = false;
                if (generation == generation_) {
                    observe_read(diagnostics_.stats, result);
                    block.evidence = make_read_evidence(result);
                    diagnostics_.latest = block.evidence;
                    diagnostics_.activity = ReadActivity::buffering;
                    PlayerEvent event;
                    event.stream_generation = generation;
                    event.read = block.evidence;
                    if (event.read.status == IntegrityReadStatus::uncertain) {
                        event.severity = EventSeverity::warning;
                        event.presentation = PresentationPriority::sticky;
                    } else if (event.read.status == IntegrityReadStatus::recovered) {
                        event.severity = EventSeverity::info;
                        event.presentation = PresentationPriority::activity;
                    }
                    if (events_.size() == read_event_capacity) {
                        events_.pop_front();
                        ++dropped_events_;
                    }
                    events_.push_back(std::move(event));
                }
                lock.unlock();
                if (result.status != ReadStatus::ok || result.frames_read != static_cast<std::size_t>(frames))
                    throw std::runtime_error("CDDA read failed lba=" + std::to_string(position) +
                                             " errno=" + std::to_string(result.native_error));
                lock.lock();
                if (closing_) return;
                if (generation != generation_) break;
                queue_.push_back(std::move(block));
                position += frames;
                lock.unlock();
            }
            if (!lock.owns_lock()) lock.lock();
            if (generation == generation_) {
                active_ = false; done_ = true;
                diagnostics_.activity = ReadActivity::complete;
            }
        } catch (const std::exception& error) {
            // Release the reader on this thread, never from a Stop handler.
            reader.reset();
            if (!lock.owns_lock()) lock.lock();
            reader_open_ = false;
            reading_ = false;
            if (generation == generation_) {
                error_ = error.what(); active_ = false; done_ = false; queue_.clear();
                diagnostics_.activity = ReadActivity::failed;
            }
        }
    }
}
