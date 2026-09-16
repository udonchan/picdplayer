#pragma once
#include "cdda_reader.hpp"
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

struct PcmBlock {
    std::uint64_t generation = 0;
    std::int32_t lba = 0;
    std::vector<std::int16_t> samples;
};
struct WorkerStatus {
    std::size_t queued;
    bool done;
    std::string error;
    std::int64_t last_read_us = 0;
    std::int64_t read_inflight_us = 0;
};

// Each block is 15 CD frames (200 ms). Four seconds absorbs short USB-drive
// recovery stalls while staying below 1 MiB of PCM on a Raspberry Pi 3.
inline constexpr std::size_t pcm_queue_capacity_blocks = 20;
inline constexpr std::size_t pcm_prebuffer_blocks = 10;

// Owns the reader exclusively on one worker. Main thread never waits for CD I/O,
// except at destruction (joining an outstanding kernel/library operation).
class PcmWorker {
public:
    using Factory = std::function<std::unique_ptr<CddaReader>()>;
    explicit PcmWorker(Factory factory);
    ~PcmWorker();
    PcmWorker(const PcmWorker&) = delete;
    PcmWorker& operator=(const PcmWorker&) = delete;
    std::uint64_t start(std::int32_t begin, std::int32_t end);
    void cancel();
    // Close the backend on its owner thread before the next read. Use after
    // media removal or a hardware error; normal pause/seek keeps it open.
    void discard_reader();
    // True once no read is in flight and the backend device handle is closed.
    bool device_released();
    bool pop(PcmBlock& block);
    WorkerStatus status();
private:
    void run();
    Factory factory_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<PcmBlock> queue_;
    bool closing_ = false, active_ = false, done_ = false;
    bool discard_reader_ = false;
    bool reader_open_ = false;
    bool reading_ = false;
    std::chrono::steady_clock::time_point read_started_{};
    std::int64_t last_read_us_ = 0;
    std::uint64_t generation_ = 0;
    std::int32_t begin_ = 0, end_ = 0;
    std::string error_;
    std::thread thread_;
};
