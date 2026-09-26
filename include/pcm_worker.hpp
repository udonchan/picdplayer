#pragma once
#include "cdda_reader.hpp"
#include "drive_access.hpp"
#include "integrity_state.hpp"
#include "player_event.hpp"
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct PcmBlock {
    std::uint64_t generation = 0;
    std::int32_t lba = 0;
    std::vector<std::int16_t> samples;
    ReadEvidence evidence;
};
struct WorkerStatus {
    std::size_t queued;
    bool done;
    std::string error;
    std::int64_t last_read_us = 0;
    std::int64_t read_inflight_us = 0;
    ReadDiagnostics diagnostics;
};

// Each block is 15 CD frames (200 ms).  The queue can retain ten seconds of
// PCM (about 1.8 MiB), while playback starts after three blocks (0.6 seconds).
inline constexpr std::size_t pcm_block_cd_frames = 15;
inline constexpr std::size_t pcm_queue_capacity_blocks = 50;
inline constexpr std::size_t pcm_prebuffer_blocks = 3;
inline constexpr std::size_t maximum_buffer_cd_frames = 30 * 75;
inline constexpr std::size_t read_event_capacity = 256;

struct PcmBufferConfig {
    std::size_t capacity_cd_frames = pcm_queue_capacity_blocks * pcm_block_cd_frames;
    std::size_t startup_cd_frames = pcm_prebuffer_blocks * pcm_block_cd_frames;
};

void validate_pcm_buffer_config(const PcmBufferConfig& config);

// Owns the reader exclusively on one worker. Main thread never waits for CD I/O,
// except at destruction (joining an outstanding kernel/library operation).
class PcmWorker {
public:
    using Factory = std::function<std::unique_ptr<CddaReader>()>;
    explicit PcmWorker(Factory factory, std::string strategy = "legacy",
                       PcmBufferConfig buffer = {},
                       std::shared_ptr<DriveAccessCoordinator> drive_access = {},
                       std::string requested_mode = "LEGACY",
                       std::size_t read_block_cd_frames = pcm_block_cd_frames);
    ~PcmWorker();
    PcmWorker(const PcmWorker&) = delete;
    PcmWorker& operator=(const PcmWorker&) = delete;
    std::uint64_t start(std::int32_t begin, std::int32_t end);
    // Applies only to a later start. It also asks the owner thread to close
    // any existing reader before that start uses a different configuration.
    void reconfigure(std::string strategy, std::string requested_mode,
                     std::size_t read_block_cd_frames);
    void cancel();
    // Close the backend on its owner thread before the next read. Use after
    // media removal or a hardware error; normal pause/seek keeps it open.
    void discard_reader();
    // True once no read is in flight and the backend device handle is closed.
    bool device_released();
    bool pop(PcmBlock& block);
    bool pop_event(PlayerEvent& event);
    WorkerStatus status(bool include_history = false);
    void set_disc_generation(std::uint64_t generation);
    std::size_t buffer_capacity_blocks() const;
    std::size_t startup_buffer_blocks() const;
    std::size_t read_block_cd_frames() const;
private:
    void run();
    Factory factory_;
    PcmBufferConfig buffer_config_;
    std::shared_ptr<DriveAccessCoordinator> drive_access_;
    std::size_t capacity_blocks_ = pcm_queue_capacity_blocks;
    std::size_t startup_blocks_ = pcm_prebuffer_blocks;
    std::size_t read_block_cd_frames_ = pcm_block_cd_frames;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<PcmBlock> queue_;
    std::deque<PlayerEvent> events_;
    bool closing_ = false, active_ = false, done_ = false;
    bool discard_reader_ = false;
    bool reader_open_ = false;
    bool drive_call_inflight_ = false;
    bool reading_ = false;
    std::chrono::steady_clock::time_point read_started_{};
    std::int64_t last_read_us_ = 0;
    ReadDiagnostics diagnostics_;
    std::uint64_t dropped_events_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t device_generation_ = 0, disc_generation_ = 0;
    std::array<ReadEvidence, read_history_capacity> history_{};
    std::size_t history_begin_ = 0, history_size_ = 0;
    std::int32_t begin_ = 0, end_ = 0;
    std::string error_;
    std::thread thread_;
};
