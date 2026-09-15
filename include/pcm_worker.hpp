#pragma once
#include "cdda_reader.hpp"
#include <condition_variable>
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
struct WorkerStatus { std::size_t queued; bool done; std::string error; };

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
    bool pop(PcmBlock& block);
    WorkerStatus status();
private:
    void run();
    Factory factory_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<PcmBlock> queue_;
    bool closing_ = false, active_ = false, done_ = false;
    std::uint64_t generation_ = 0;
    std::int32_t begin_ = 0, end_ = 0;
    std::string error_;
    std::thread thread_;
};
