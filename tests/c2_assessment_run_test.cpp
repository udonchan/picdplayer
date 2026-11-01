#include "c2_assessment_run.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("C2 run invariant failed"); }
const std::string zero = "f81c4fa3aa1ad49efe00502d9d9a92330a660f1b0325d9184f23bf478e96e22e";
const std::string one = "50a67a54d673811aca2c2c625ddb1eced284d7cd44184301931cb0c3bab214c3";
C2Reference reference() { return {"synthetic", "independent synthetic source", true, true, 0, {{10, zero}, {20, one}}}; }
struct State {
    unsigned opens = 0, reads = 0, closes = 0;
    bool current = true, flagged = true, unknown = false, cancel_read = false, late_read = false, cancel_close = false;
    std::chrono::steady_clock::time_point clock{};
};
class Reader final : public CddaReader {
    State& state_;
    std::int32_t lba_ = 0;
public:
    explicit Reader(State& state) : state_(state) { ++state_.opens; }
    ~Reader() override { ++state_.closes; if (state_.cancel_close) state_.current = false; }
    void seek(std::int32_t lba) override { lba_ = lba; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        check(pcm.size() == cdda_samples_per_frame);
        ++state_.reads;
        std::fill(pcm.begin(), pcm.end(), 0);
        ReadResult result{lba_, 1, 1, ReadStatus::ok, 0, 0};
        result.c2_status = state_.unknown ? C2Status::unknown :
            state_.flagged && lba_ == 20 ? C2Status::reported : C2Status::clean;
        if (state_.cancel_read) state_.current = false;
        if (state_.late_read) state_.clock += std::chrono::seconds(5);
        return result;
    }
};
C2AssessmentRunResult run(State& state, const C2Reference& ref) {
    return run_c2_assessment_raw(ref, [&] { return std::make_unique<Reader>(state); },
        [&] { return state.current; }, [&] { return state.clock; });
}
}

int main() {
    try {
        State positive;
        const auto accepted = run(positive, reference());
        check(accepted.state == C2AssessmentRunState::complete && accepted.assessment.value == Knowledge::yes);
        check(positive.reads == 6 && positive.closes == 1 && accepted.counts.clean_equal == 3 && accepted.counts.flagged_different == 3);
        State counterexample;
        counterexample.flagged = false;
        check(run(counterexample, reference()).assessment.value == Knowledge::no);
        State no_challenge;
        no_challenge.flagged = false;
        auto same = reference();
        same.sectors[1].sha256 = zero;
        check(run(no_challenge, same).assessment.value == Knowledge::unknown);
        State unknown;
        unknown.unknown = true;
        check(run(unknown, reference()).state == C2AssessmentRunState::failed);
        check(unknown.reads == 1 && unknown.closes == 1);
        State cancelled;
        cancelled.current = false;
        check(run(cancelled, reference()).state == C2AssessmentRunState::cancelled && cancelled.opens == 0);
        State interrupted;
        interrupted.cancel_read = true;
        const auto discarded = run(interrupted, reference());
        check(discarded.state == C2AssessmentRunState::cancelled && discarded.assessment.value == Knowledge::unknown);
        check(interrupted.reads == 1 && interrupted.closes == 1);
        State late;
        late.late_read = true;
        check(run(late, reference()).state == C2AssessmentRunState::timed_out);
        check(late.reads == 1 && late.closes == 1);
        State closing;
        closing.cancel_close = true;
        const auto retired = run(closing, reference());
        check(retired.state == C2AssessmentRunState::cancelled && retired.assessment.value == Knowledge::unknown);
        check(closing.reads == 6 && closing.closes == 1);
        State invalid;
        auto absent = reference();
        absent.offset_samples.reset();
        check(run(invalid, absent).state == C2AssessmentRunState::failed && invalid.opens == 0);
        std::cout << "PASS: conditional reference runner, unavailable/cancel/deadline and reader close\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
