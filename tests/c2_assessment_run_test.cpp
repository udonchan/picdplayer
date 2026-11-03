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
    std::chrono::milliseconds read_cost{0}, close_cost{0};
};
class Reader final : public CddaReader {
    State& state_;
    std::int32_t lba_ = 0;
public:
    explicit Reader(State& state) : state_(state) { ++state_.opens; }
    ~Reader() override { ++state_.closes; state_.clock += state_.close_cost;
        if (state_.cancel_close) state_.current = false; }
    void seek(std::int32_t lba) override { lba_ = lba; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        check(pcm.size() == cdda_samples_per_frame);
        ++state_.reads;
        state_.clock += state_.read_cost;
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

void orchestration_cases() {
    // Synthetic digests/PCM only: not an independent reference for any real disc.
    const DiscToc toc{{{1, 0, 75}}, 75};
    auto ref = reference();
    ref.expected_toc = toc;
    for (unsigned variant = 0; variant < 9; ++variant) {
        State state;
        unsigned tocs = 0;
        bool threw = false;
        C2AssessmentRunResult result;
        state.cancel_read = variant == 7;
        state.cancel_close = variant == 8;
        try {
            result = run_c2_assessment_with_toc(ref, [&] {
                check(tocs == 1 && state.opens == 0);
                return std::make_unique<Reader>(state);
            }, [&] {
                ++tocs;
                check(tocs <= 2 && (tocs == 1 ? state.opens == 0 : state.closes == 1));
                auto observed = toc;
                if ((variant == 1 && tocs == 1) || (variant == 2 && tocs == 2))
                    ++observed.leadout_lba;
                if ((variant == 3 && tocs == 1) || (variant == 4 && tocs == 2))
                    state.clock += std::chrono::seconds(4);
                if ((variant == 5 && tocs == 1) || (variant == 6 && tocs == 2))
                    state.current = false;
                return observed;
            }, [&] { return state.current; }, [&] { return state.clock; });
        } catch (const std::runtime_error& error) {
            threw = true;
            check(std::string(error.what()) == (variant == 1 || variant == 5
                ? "C2 reference TOC context changed before assessment"
                : "C2 reference TOC context changed after assessment"));
        }
        const bool no_open = variant == 1 || variant == 3 || variant == 5;
        check(state.opens == (no_open ? 0u : 1u) && state.closes == state.opens);
        check(state.reads == (no_open ? 0u : variant == 7 ? 1u : 6u));
        check(tocs == (no_open || variant >= 7 ? 1u : 2u));
        check(threw == (variant == 1 || variant == 2 || variant == 5 || variant == 6));
        if (variant == 0) {
            check(result.state == C2AssessmentRunState::complete && result.assessment.value == Knowledge::yes);
            check(result.counts.samples == 6 && result.counts.clean_equal == 3 && result.counts.flagged_different == 3);
        } else {
            check(result.assessment.value == Knowledge::unknown);
            if (variant == 3 || variant == 4) check(result.state == C2AssessmentRunState::timed_out);
            if (variant >= 7) check(result.state == C2AssessmentRunState::cancelled);
        }
    }
}

void cumulative_deadline_cases() {
    // Every individual phase is shorter than four seconds; only the shared budget expires.
    const DiscToc toc{{{1, 0, 75}}, 75};
    auto ref = reference();
    ref.expected_toc = toc;
    for (unsigned variant = 0; variant < 4; ++variant) {
        State state;
        state.read_cost = std::chrono::milliseconds(variant == 2 ? 800 : 300);
        state.close_cost = std::chrono::milliseconds(variant == 3 ? 700 : 200);
        unsigned tocs = 0;
        const auto result = run_c2_assessment_with_toc(ref,
            [&] { return std::make_unique<Reader>(state); }, [&] {
                ++tocs;
                state.clock += std::chrono::milliseconds(tocs == 1 ? 1500 : variant == 0 ? 499 : 500);
                return toc;
            }, [&] { return state.current; }, [&] { return state.clock; });
        check(state.opens == 1 && state.closes == 1);
        check(state.reads == (variant == 2 ? 4u : 6u));
        check(tocs == (variant >= 2 ? 1u : 2u));
        if (variant == 0) {
            check(result.state == C2AssessmentRunState::complete && result.assessment.value == Knowledge::yes);
            check(state.clock.time_since_epoch() == std::chrono::milliseconds(3999));
        } else {
            check(result.state == C2AssessmentRunState::timed_out && result.assessment.value == Knowledge::unknown);
            check(result.counts.samples == (variant == 2 ? 3u : 6u));
        }
    }
}
}

int main() {
    try {
        orchestration_cases();
        cumulative_deadline_cases();
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
