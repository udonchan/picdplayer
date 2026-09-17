#include "cdda_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("repeated verifier test failed"); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid operation accepted");
}

struct Reply { int value; ReadStatus status = ReadStatus::ok; int error = 0; };
class ScriptedReader final : public CddaReader {
public:
    explicit ScriptedReader(std::vector<Reply> replies) : replies_(std::move(replies)) {}
    void seek(std::int32_t lba) override { positions.push_back(lba); position_ = lba; positioned_ = true; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("seek required");
        if (next_ >= replies_.size()) throw std::runtime_error("script exhausted");
        const auto reply = replies_[next_++];
        const auto frames = pcm.size() / cdda_samples_per_frame;
        ReadResult result{position_, frames, 0, reply.status, reply.error, 0};
        if (reply.status == ReadStatus::ok) {
            std::fill(pcm.begin(), pcm.end(), static_cast<std::int16_t>(reply.value));
            result.frames_read = frames;
            position_ += static_cast<std::int32_t>(frames);
        } else positioned_ = false;
        return result;
    }
    std::vector<std::int32_t> positions;
private:
    std::vector<Reply> replies_;
    std::size_t next_ = 0;
    std::int32_t position_ = 0;
    bool positioned_ = false;
};
}

int main() {
    try {
        rejects([] { validate_repeated_read_policy({1, 3, std::chrono::milliseconds(100)}); });
        rejects([] { validate_repeated_read_policy({3, 2, std::chrono::milliseconds(100)}); });
        rejects([] { validate_repeated_read_policy({2, 9, std::chrono::milliseconds(100)}); });

        std::vector<std::int16_t> pcm(cdda_samples_per_frame, -1);
        auto equal_base = std::make_unique<ScriptedReader>(std::vector<Reply>{{7}, {7}});
        auto* equal_observer = equal_base.get();
        auto equal = make_repeated_read_verifier(std::move(equal_base));
        equal->seek(100);
        auto result = equal->read(pcm);
        check(result.status == ReadStatus::ok && result.frames_read == 1 && pcm.front() == 7);
        check(result.verification.attempts == 2 && result.verification.complete_reads == 2);
        check(result.verification.matching_reads == 2 && result.verification.mismatches == 0);
        check(equal_observer->positions == std::vector<std::int32_t>({100, 100, 100}));
        // Logical cursor advances even though each physical attempt seeks back.
        auto next_pcm = pcm;
        // Supply of scripted replies is intentionally exhausted: seek still shows position 101.
        try { (void)equal->read(next_pcm); } catch (const std::runtime_error&) {}
        check(equal_observer->positions.back() == 101);

        auto majority = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1}, {2}, {2}}));
        majority->seek(300);
        std::fill(pcm.begin(), pcm.end(), -1);
        result = majority->read(pcm);
        check(result.status == ReadStatus::ok && pcm.front() == 2);
        check(result.verification.attempts == 3 && result.verification.matching_reads == 2);
        check(result.verification.mismatches == 1);

        auto unresolved = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1}, {2}, {3}}));
        unresolved->seek(0);
        std::fill(pcm.begin(), pcm.end(), -1);
        result = unresolved->read(pcm);
        check(result.status == ReadStatus::read_error && result.frames_read == 0);
        check(result.verification.attempts == 3 && result.verification.mismatches == 2);
        check(pcm.front() == -1);
        rejects([&] { unresolved->read(pcm); });

        auto after_error = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{0, ReadStatus::read_error, EIO},
                                                                 {9}, {9}}));
        after_error->seek(42);
        result = after_error->read(pcm);
        check(result.status == ReadStatus::ok && result.verification.attempts == 3);
        check(result.verification.complete_reads == 2 && pcm.front() == 9);

        auto time = std::chrono::steady_clock::time_point{};
        auto budget = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1}, {1}}),
            {2, 3, std::chrono::milliseconds(10)}, [&] {
                const auto current = time;
                time += std::chrono::milliseconds(10);
                return current;
            });
        budget->seek(0);
        result = budget->read(pcm);
        check(result.status == ReadStatus::read_error);
        check(result.verification.attempts == 1 && result.verification.time_budget_exhausted);

        std::cout << "PASS: bounded repeated reads, full PCM consensus and fail-closed output\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
