#include "pcm_worker.hpp"
#include <algorithm>
#include <stdexcept>

void validate_pcm_buffer_config(const PcmBufferConfig& config) {
    if (config.capacity_cd_frames < pcm_block_cd_frames ||
        config.capacity_cd_frames > maximum_buffer_cd_frames ||
        config.capacity_cd_frames % pcm_block_cd_frames != 0)
        throw std::invalid_argument("buffer capacity must be a multiple of 15 CD frames in range 15..2250");
    if (config.startup_cd_frames < pcm_block_cd_frames ||
        config.startup_cd_frames > config.capacity_cd_frames ||
        config.startup_cd_frames % pcm_block_cd_frames != 0)
        throw std::invalid_argument("startup buffer must be a multiple of 15 CD frames and not exceed capacity");
}

PcmWorker::PcmWorker(Factory factory, std::string strategy, PcmBufferConfig buffer,
                     std::shared_ptr<DriveAccessCoordinator> drive_access,
                     std::string requested_mode, std::size_t read_block_cd_frames)
    : factory_(std::move(factory)), buffer_config_(buffer), drive_access_(std::move(drive_access)) {
    if (!factory_) throw std::invalid_argument("reader factory is empty");
    validate_pcm_buffer_config(buffer_config_);
    if (read_block_cd_frames < pcm_block_cd_frames ||
        read_block_cd_frames % pcm_block_cd_frames != 0 ||
        read_block_cd_frames > buffer_config_.capacity_cd_frames)
        throw std::invalid_argument("read block must be a multiple of 15 CD frames and fit the buffer");
    read_block_cd_frames_ = read_block_cd_frames;
    capacity_blocks_ = buffer_config_.capacity_cd_frames / read_block_cd_frames_;
    startup_blocks_ = std::min(capacity_blocks_,
        (buffer_config_.startup_cd_frames + read_block_cd_frames_ - 1) / read_block_cd_frames_);
    diagnostics_.policy_revision++;
    diagnostics_.effective_strategy = std::move(strategy);
    diagnostics_.requested_mode = std::move(requested_mode);
    diagnostics_.buffer_capacity_frames = buffer_config_.capacity_cd_frames;
    diagnostics_.startup_buffer_frames = buffer_config_.startup_cd_frames;
    diagnostics_.read_block_frames = read_block_cd_frames_;
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
    diagnostics_.stream_generation = generation_;
    diagnostics_.coverage = {};
    history_begin_ = history_size_ = 0;
    diagnostics_.history_evicted = 0;
    events_.clear(); dropped_events_ = 0;
    changed_.notify_all();
    return generation_;
}
void PcmWorker::reconfigure(std::string strategy, std::string requested_mode,
                            std::size_t read_block_cd_frames) {
    std::lock_guard lock(mutex_);
    if (read_block_cd_frames < pcm_block_cd_frames ||
        read_block_cd_frames % pcm_block_cd_frames != 0 ||
        read_block_cd_frames > buffer_config_.capacity_cd_frames)
        throw std::invalid_argument("read block must be a multiple of 15 CD frames and fit the buffer");
    read_block_cd_frames_ = read_block_cd_frames;
    capacity_blocks_ = buffer_config_.capacity_cd_frames / read_block_cd_frames_;
    startup_blocks_ = std::min(capacity_blocks_,
        (buffer_config_.startup_cd_frames + read_block_cd_frames_ - 1) / read_block_cd_frames_);
    diagnostics_.policy_revision++;
    diagnostics_.effective_strategy = std::move(strategy);
    diagnostics_.requested_mode = std::move(requested_mode);
    diagnostics_.read_block_frames = read_block_cd_frames_;
    discard_reader_ = true;
    changed_.notify_all();
}
std::size_t PcmWorker::buffer_capacity_blocks() const {
    std::lock_guard lock(mutex_); return capacity_blocks_;
}
std::size_t PcmWorker::startup_buffer_blocks() const {
    std::lock_guard lock(mutex_); return startup_blocks_;
}
std::size_t PcmWorker::read_block_cd_frames() const {
    std::lock_guard lock(mutex_); return read_block_cd_frames_;
}
void PcmWorker::cancel() {
    std::lock_guard lock(mutex_);
    ++generation_; active_ = false; done_ = false;
    diagnostics_.stream_generation = generation_;
    diagnostics_.coverage = {};
    history_begin_ = history_size_ = 0;
    diagnostics_.history_evicted = 0;
    diagnostics_.stats = {};
    queue_.clear(); error_.clear();
    diagnostics_.activity = ReadActivity::idle;
    diagnostics_.latest.reset();
    events_.clear();
    changed_.notify_all();
}
void PcmWorker::discard_reader() {
    std::lock_guard lock(mutex_);
    ++generation_; active_ = false; done_ = false;
    diagnostics_.stream_generation = generation_;
    diagnostics_.coverage = {};
    history_begin_ = history_size_ = 0;
    diagnostics_.history_evicted = 0;
    diagnostics_.stats = {};
    queue_.clear(); error_.clear();
    diagnostics_.activity = ReadActivity::idle;
    diagnostics_.latest.reset();
    events_.clear();
    discard_reader_ = true;
    changed_.notify_all();
}
bool PcmWorker::device_released() {
    std::lock_guard lock(mutex_);
    return !active_ && !drive_call_inflight_ && !discard_reader_ && !reader_open_;
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
void PcmWorker::set_disc_generation(std::uint64_t generation) {
    std::lock_guard lock(mutex_);
    if (active_) throw std::logic_error("disc generation change requires stopped worker");
    disc_generation_ = generation;
}
WorkerStatus PcmWorker::status(bool include_history) {
    std::lock_guard lock(mutex_);
    const auto inflight = reading_ ? std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - read_started_).count() : 0;
    diagnostics_.dropped_events = dropped_events_;
    auto diagnostics = diagnostics_;
    diagnostics.history_included = include_history;
    if (include_history) {
        diagnostics.recent_reads.reserve(history_size_);
        for (std::size_t i = 0; i < history_size_; ++i)
            diagnostics.recent_reads.push_back(history_[(history_begin_ + i) % read_history_capacity]);
    }
    return {queue_.size(), done_, error_, last_read_us_, inflight, std::move(diagnostics)};
}
void PcmWorker::run() {
    std::unique_ptr<CddaReader> reader;
    for (;;) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return closing_ || active_ || discard_reader_; });
        if (closing_) return;
        if (discard_reader_) {
            discard_reader_ = false;
            drive_call_inflight_ = true;
            lock.unlock();
            // Never close a device while holding the queue mutex.
            if (drive_access_) drive_access_->invoke([&] { reader.reset(); });
            else reader.reset();
            lock.lock();
            drive_call_inflight_ = false;
            reader_open_ = false;
            lock.unlock();
            continue; // Recheck shutdown and any newer Start after slow close.
        }
        const auto generation = generation_;
        const auto policy_revision = diagnostics_.policy_revision;
        const auto disc_generation = disc_generation_;
        auto position = begin_;
        const auto end = end_;
        // A configuration is immutable for the lifetime of this stream
        // generation.  reconfigure() is only used after the session has
        // stopped this generation, but keep local copies so an old in-flight
        // worker never races with its replacement configuration.
        const auto capacity_blocks = capacity_blocks_;
        const auto read_block_cd_frames = read_block_cd_frames_;
        lock.unlock();
        try {
            if (!reader) {
                lock.lock(); drive_call_inflight_ = true; lock.unlock();
                reader = drive_access_ ? drive_access_->invoke(factory_) : factory_();
                lock.lock();
                drive_call_inflight_ = false;
                reader_open_ = static_cast<bool>(reader);
                if (reader_open_) ++device_generation_;
                lock.unlock();
            }
            if (!reader) throw std::runtime_error("reader factory returned null");
            // A slow open may have been superseded by Stop/Seek.
            lock.lock();
            if (closing_) return;
            if (generation != generation_) continue;
            lock.unlock();
            lock.lock(); drive_call_inflight_ = true; lock.unlock();
            if (drive_access_) drive_access_->invoke([&] { reader->seek(position); });
            else reader->seek(position);
            lock.lock(); drive_call_inflight_ = false; lock.unlock();
            while (position < end) {
                lock.lock();
                changed_.wait(lock, [&] {
                    return closing_ || generation != generation_ ||
                           queue_.size() < capacity_blocks;
                });
                if (closing_) return;
                if (generation != generation_) break;
                reading_ = true;
                drive_call_inflight_ = true;
                diagnostics_.activity = ReadActivity::reading;
                read_started_ = std::chrono::steady_clock::now();
                lock.unlock();
                const auto frames = std::min<std::int32_t>(
                    static_cast<std::int32_t>(read_block_cd_frames), end - position);
                PcmBlock block{generation, position,
                               std::vector<std::int16_t>(frames * cdda_samples_per_frame), {}};
                const auto result = drive_access_
                    ? drive_access_->invoke([&] { return reader->read(block.samples); })
                    : reader->read(block.samples);
                lock.lock();
                last_read_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - read_started_).count();
                reading_ = false;
                drive_call_inflight_ = false;
                if (generation == generation_) {
                    observe_read(diagnostics_.stats, result);
                    diagnostics_.coverage.observe(result);
                    block.evidence = make_read_evidence(result);
                    block.evidence.stream_generation = generation;
                    block.evidence.policy_revision = policy_revision;
                    block.evidence.device_generation = device_generation_;
                    block.evidence.disc_generation = disc_generation;
                    block.evidence.read_sequence = diagnostics_.stats.read_calls;
                    if (history_size_ == read_history_capacity) {
                        history_begin_ = (history_begin_ + 1) % read_history_capacity;
                        --history_size_;
                        ++diagnostics_.history_evicted;
                    }
                    history_[(history_begin_ + history_size_++) % read_history_capacity] = block.evidence;
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
            if (drive_access_) drive_access_->invoke([&] { reader.reset(); });
            else reader.reset();
            if (!lock.owns_lock()) lock.lock();
            reader_open_ = false;
            reading_ = false;
            drive_call_inflight_ = false;
            if (generation == generation_) {
                error_ = error.what(); active_ = false; done_ = false; queue_.clear();
                diagnostics_.activity = ReadActivity::failed;
            }
        }
    }
}
