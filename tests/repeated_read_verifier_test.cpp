#include "cdda_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check_impl(bool value, int line) {
    if (!value) throw std::runtime_error("repeated verifier test failed at line " + std::to_string(line));
}
#define check(value) check_impl((value), __LINE__)
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid operation accepted");
}

struct Reply {
    Reply(int value, ReadStatus status = ReadStatus::ok, int error = 0, int start_lba_offset = 0,
          std::optional<std::size_t> frames_read = {})
        : value(value), status(status), error(error), start_lba_offset(start_lba_offset),
          frames_read(frames_read) {}
    int value;
    ReadStatus status;
    int error;
    int start_lba_offset;
    std::optional<std::size_t> frames_read;
};
class ScriptedReader final : public CddaReader {
public:
    explicit ScriptedReader(std::vector<Reply> replies) : replies_(std::move(replies)) {}
    void seek(std::int32_t lba) override { positions.push_back(lba); position_ = lba; positioned_ = true; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("seek required");
        if (next_ >= replies_.size()) throw std::runtime_error("script exhausted");
        const auto reply = replies_[next_++];
        const auto frames = pcm.size() / cdda_samples_per_frame;
        const auto read_frames = std::min(frames, reply.frames_read.value_or(frames));
        ReadResult result{position_ + reply.start_lba_offset, frames, 0, reply.status, reply.error, 0};
        if (reply.status == ReadStatus::ok) {
            std::fill_n(pcm.begin(), static_cast<std::ptrdiff_t>(read_frames * cdda_samples_per_frame),
                        static_cast<std::int16_t>(reply.value));
            result.frames_read = read_frames;
            position_ += static_cast<std::int32_t>(read_frames);
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

class PositionReader final : public CddaReader {
public:
    void seek(std::int32_t lba) override { positions.push_back(lba); position_ = lba; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        const auto frames = pcm.size() / cdda_samples_per_frame;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            std::fill_n(pcm.begin() + static_cast<std::ptrdiff_t>(frame * cdda_samples_per_frame),
                        cdda_samples_per_frame, static_cast<std::int16_t>(position_ + frame));
        }
        ReadResult result{position_, frames, frames, ReadStatus::ok, 0, 0};
        position_ += static_cast<std::int32_t>(frames);
        return result;
    }
    std::vector<std::int32_t> positions;
private:
    std::int32_t position_ = 0;
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
        // The second logical block asks for one preceding frame, so its
        // physical seek begins at the prior block's final LBA.
        try { (void)equal->read(next_pcm); } catch (const std::runtime_error&) {}
        check(equal_observer->positions.back() == 100);

        auto majority = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1}, {2}, {2}}));
        majority->seek(300);
        std::fill(pcm.begin(), pcm.end(), -1);
        result = majority->read(pcm);
        check(result.status == ReadStatus::ok && pcm.front() == 2);
        check(result.verification.attempts == 3 && result.verification.matching_reads == 2);
        check(result.verification.mismatches == 1);
        check(result.verification.detail_count == 3);
        check(result.verification.details[0].candidate == 1);
        check(result.verification.details[1].candidate == 2);
        check(result.verification.details[2].candidate == 2);
        check(result.verification.accepted_candidate == 2);
        check(result.verification.accepted_attempt == 3);

        auto unresolved = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1}, {2}, {3}}));
        unresolved->seek(0);
        std::fill(pcm.begin(), pcm.end(), -1);
        result = unresolved->read(pcm);
        check(result.status == ReadStatus::read_error && result.frames_read == 0);
        check(result.verification.attempts == 3 && result.verification.mismatches == 2);
        check(pcm.front() == -1);
        check(!result.verification.accepted_candidate && !result.verification.accepted_attempt);
        rejects([&] { unresolved->read(pcm); });

        auto after_error = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{0, ReadStatus::read_error, EIO},
                                                                 {9}, {9}}));
        after_error->seek(42);
        result = after_error->read(pcm);
        check(result.status == ReadStatus::ok && result.verification.attempts == 3);
        check(result.verification.complete_reads == 2 && pcm.front() == 9);
        check(!result.verification.details[0].complete);
        check(result.verification.details[0].native_error == EIO);
        check(!result.verification.details[0].candidate);
        check(result.verification.accepted_candidate == 1);

        auto partial_then_equal = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{5, ReadStatus::ok, 0, 0, 0}, {5}, {5}}));
        partial_then_equal->seek(55);
        result = partial_then_equal->read(pcm);
        check(result.status == ReadStatus::ok && result.verification.attempts == 3);
        check(result.verification.complete_reads == 2 && !result.verification.details[0].complete);
        check(result.verification.details[0].frames_read == 0 && pcm.front() == 5);

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

        auto bounded = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1},{2},{3},{4},{5},{6},{7},{8}}),
            {2, 8, std::chrono::milliseconds(10000)});
        bounded->seek(0);
        result = bounded->read(pcm);
        check(result.verification.detail_count == maximum_verification_attempts);
        check(!result.verification.accepted_candidate);

        auto shifted = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{1, ReadStatus::ok, 0, 1},
                                                                 {1, ReadStatus::ok, 0, 1}}),
            {2, 2, std::chrono::milliseconds(100), 0});
        shifted->seek(70);
        result = shifted->read(pcm);
        check(result.status == ReadStatus::read_error && result.verification.complete_reads == 0);
        check(result.verification.details[0].physical_start_lba == 70);
        check(result.verification.details[0].observed_start_lba == 71);

        auto position_base = std::make_unique<PositionReader>();
        auto* position_observer = position_base.get();
        auto overlap = make_repeated_read_verifier(std::move(position_base),
                                                    {2, 2, std::chrono::milliseconds(100), 1});
        std::vector<std::int16_t> two_frames(2 * cdda_samples_per_frame, -1);
        overlap->seek(100);
        result = overlap->read(two_frames);
        check(result.status == ReadStatus::ok && result.verification.overlap ==
              OverlapVerification::stream_boundary);
        check(two_frames.front() == 100 && two_frames[cdda_samples_per_frame] == 101);
        result = overlap->read(two_frames);
        check(result.status == ReadStatus::ok && result.verification.overlap ==
              OverlapVerification::matched);
        check(result.verification.overlap_frames_compared == 1);
        check(two_frames.front() == 102 && two_frames[cdda_samples_per_frame] == 103);
        check(position_observer->positions == std::vector<std::int32_t>({100, 100, 100, 101, 101}));
        check(result.read_independence == ReadIndependence::cache_possible);

        auto mismatch = make_repeated_read_verifier(
            std::make_unique<ScriptedReader>(std::vector<Reply>{{7}, {7}, {8}, {8}}),
            {2, 2, std::chrono::milliseconds(100), 1});
        mismatch->seek(0);
        std::vector<std::int16_t> one_frame(cdda_samples_per_frame, -1);
        check(mismatch->read(one_frame).status == ReadStatus::ok && one_frame.front() == 7);
        std::fill(one_frame.begin(), one_frame.end(), -1);
        result = mismatch->read(one_frame);
        check(result.status == ReadStatus::read_error && result.native_error == EILSEQ);
        check(result.verification.overlap == OverlapVerification::mismatched);
        check(one_frame.front() == -1);
        rejects([&] { mismatch->read(one_frame); });

        std::cout << "PASS: bounded repeated reads, position and overlap continuity, and fail-closed output\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
