#include "c2_assessment_session.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("C2 session invariant failed"); }
void check(ApiCommandResult value) { check(value.accepted); }
template<class F> void wait(F done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("C2 session wait timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, open = false;
    void pass() { std::unique_lock lock(mutex); entered = true; changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(3), [&] { return open; })) throw std::runtime_error("C2 gate timeout"); }
    void release() { std::lock_guard lock(mutex); open = true; changed.notify_all(); }
    void await_entry() { std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; })); }
    ~Gate() { release(); }
};
struct SyntheticState {
    std::atomic<unsigned> opens{0}, reads{0}, closes{0}, tocs{0};
    std::atomic<int> fd{-1};
    std::atomic<bool> fd_closed{false};
    Gate* read_gate = nullptr;
    Gate* close_gate = nullptr;
};
// Regular tempfile PCM and synthetic C2 flags, not Pi independent-reference evidence.
class SyntheticReader final : public CddaReader {
    SyntheticState& state_;
    std::FILE* file_;
    std::int32_t lba_ = 0;
public:
    explicit SyntheticReader(SyntheticState& state) : state_(state), file_(std::tmpfile()) {
        if (!file_) throw std::runtime_error("synthetic tempfile unavailable");
        std::array<std::int16_t, cdda_samples_per_frame> zero{};
        if (std::fwrite(zero.data(), sizeof(zero), 1, file_) != 1) {
            std::fclose(file_); throw std::runtime_error("synthetic tempfile write failed");
        }
        state_.fd = ::fileno(file_);
        ++state_.opens;
    }
    ~SyntheticReader() override {
        // A bounded close gate exercises retirement, not forced I/O cancellation.
        if (state_.close_gate) {
            try { state_.close_gate->pass(); } catch (...) { std::terminate(); }
        }
        const int fd = ::fileno(file_);
        const bool closed = std::fclose(file_) == 0;
        errno = 0;
        state_.fd_closed = closed && ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
        ++state_.closes;
    }
    void seek(std::int32_t lba) override { lba_ = lba; std::rewind(file_); }
    ReadResult read(std::span<std::int16_t> pcm) override {
        ++state_.reads;
        if (state_.read_gate) state_.read_gate->pass();
        check(pcm.size() == cdda_samples_per_frame);
        check(std::fread(pcm.data(), pcm.size_bytes(), 1, file_) == 1);
        ReadResult result{lba_, 1, 1, ReadStatus::ok, 0, 0};
        result.c2_status = lba_ == 20 ? C2Status::reported : C2Status::clean;
        return result;
    }
};
struct Fixture {
    DiscToc toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
    std::atomic<unsigned> calls{0};
    Gate* gate = nullptr;
    SyntheticState synthetic;
    bool real_run;
    const unsigned toc_failure;
    MediaWorker worker;
    C2AssessmentSession session;
    C2AssessmentContext base;
    explicit Fixture(bool real = false, unsigned failure = 0) : real_run(real), toc_failure(failure),
        worker(std::make_shared<DriveAccessCoordinator>(), [] { return MediaObservation::audio_disc; },
        [this] { return toc; }, [] {}, {}, {}, {}, {}, {},
        [this](const C2Reference& reference, const auto& current) {
            if (real_run) {
                // Only this worker callback owns and advances the fake clock.
                std::chrono::steady_clock::time_point clock{};
                return run_c2_assessment_with_toc(reference,
                    [this] { return std::make_unique<SyntheticReader>(synthetic); },
                    [this, &clock] {
                        const auto call = ++synthetic.tocs;
                        auto observed = toc;
                        if ((toc_failure == 1 && call == 1) || (toc_failure == 2 && call == 2))
                            ++observed.leadout_lba;
                        if ((toc_failure == 3 && call == 1) || (toc_failure == 4 && call == 2))
                            clock += std::chrono::seconds(4);
                        return observed;
                    }, current, [&clock] { return clock; });
            }
            ++calls; if (gate) gate->pass();
            return C2AssessmentRunResult{current() ? C2AssessmentRunState::complete : C2AssessmentRunState::cancelled,
                {Knowledge::yes, CapabilityEvidenceSource::tested, "conditional synthetic reference scope"}, {}, {}};
        }), session("session", worker) {
        worker.invalidate(1);
        base.input.gate = {true, true, true, true, true};
        base.input.toc = &toc;
        base.input.device_current = true;
        base.input.disc_generation = 2;
        base.input.observation_generation = 1;
        base.input.policy_revision = 3;
        base.input.offset_samples = 0;
        base.direct = base.c2_supported = base.capture_idle = true;
    }
    ~Fixture() {
        if (gate) gate->release();
        if (synthetic.read_gate) synthetic.read_gate->release();
        if (synthetic.close_gate) synthetic.close_gate->release();
        (void)session.interrupt();
    }
    C2AssessmentContext context() {
        auto c = base;
        c.input.media_idle = worker.idle(); c.input.media_activity = worker.activity();
        return c;
    }
    ApiCommand start() {
        ApiCommand command{};
        command.type = ApiCommandType::start_c2_assessment;
        command.session_id = "session"; command.disc_generation = 2;
        command.observation_generation = base.input.observation_generation;
        command.c2_reference = std::make_shared<C2Reference>(C2Reference{
            "synthetic", "independent synthetic fixture", true, true, 0, {{10, std::string(64, 'a')}}});
        if (real_run) command.c2_reference = std::make_shared<C2Reference>(C2Reference{
            "synthetic-only", "synthetic PCM/flags; no real disc independence claim", true, true, 0,
            {{10, "f81c4fa3aa1ad49efe00502d9d9a92330a660f1b0325d9184f23bf478e96e22e"},
             {20, "50a67a54d673811aca2c2c625ddb1eced284d7cd44184301931cb0c3bab214c3"}}});
        return command;
    }
    ApiCommand cancel(std::uint64_t id) {
        ApiCommand command{}; command.type = ApiCommandType::cancel_c2_assessment;
        command.session_id = "session"; command.job_id = id;
        return command;
    }
    void offer() {
        MediaWorkerResult result{};
        wait([&] { return worker.pop(result); });
        session.offer(result);
    }
};

void integrated_cases() {
    Fixture published(true);
    check(published.session.start(published.start(), published.context()));
    published.offer();
    check(published.synthetic.opens == 1 && published.synthetic.reads == 6 &&
        published.synthetic.closes == 1 && published.synthetic.tocs == 2);
    check(published.session.busy() && published.session.released());
    check(published.session.flag(published.context()).value == Knowledge::unknown);
    published.session.poll(published.context());
    check(!published.session.busy() && published.session.flag(published.context()).value == Knowledge::yes);

    for (unsigned variant = 1; variant <= 4; ++variant) {
        Fixture rejected(true, variant);
        check(rejected.session.start(rejected.start(), rejected.context()));
        rejected.offer();
        const bool before = variant == 1 || variant == 3;
        check(rejected.synthetic.opens == (before ? 0u : 1u));
        check(rejected.synthetic.closes == (before ? 0u : 1u));
        check(rejected.synthetic.reads == (before ? 0u : 6u));
        check(rejected.synthetic.tocs == (before ? 1u : 2u));
        check(rejected.session.busy() && rejected.session.released());
        check(rejected.session.flag(rejected.context()).value == Knowledge::unknown);
        rejected.session.poll(rejected.context());
        check(!rejected.session.busy() && rejected.session.released() && rejected.worker.idle());
        const auto flag = rejected.session.flag(rejected.context());
        check(flag.value == Knowledge::unknown && flag.source == CapabilityEvidenceSource::none);
        const auto response = nlohmann::json::parse(rejected.session.response(rejected.context()).body);
        check(response["state"] == (variant <= 2 ? "FAILED" : "TIMED_OUT"));
        check(response["value"] == "UNKNOWN");
        rejected.session.poll(rejected.context());
        check(rejected.session.flag(rejected.context()).value == Knowledge::unknown);
    }

    Gate reading, closing;
    Fixture cancelled(true);
    cancelled.synthetic.read_gate = &reading;
    cancelled.synthetic.close_gate = &closing;
    check(cancelled.session.start(cancelled.start(), cancelled.context()));
    reading.await_entry();
    check(cancelled.session.cancel(cancelled.cancel(1)));
    check(cancelled.session.busy() && !cancelled.session.released());
    check(!cancelled.session.start(cancelled.start(), cancelled.context()));
    reading.release(); closing.await_entry();
    cancelled.session.poll(cancelled.context());
    check(cancelled.session.busy() && !cancelled.session.released() && cancelled.synthetic.closes == 0);
    check(::fcntl(cancelled.synthetic.fd.load(), F_GETFD) >= 0);
    check(cancelled.session.flag(cancelled.context()).value == Knowledge::unknown);
    closing.release();
    wait([&] { return cancelled.worker.idle(); });
    cancelled.session.poll(cancelled.context());
    check(!cancelled.session.busy() && cancelled.session.released());
    check(cancelled.synthetic.opens == 1 && cancelled.synthetic.reads == 1 &&
        cancelled.synthetic.closes == 1 && cancelled.synthetic.tocs == 1 && cancelled.synthetic.fd_closed);
    check(cancelled.session.flag(cancelled.context()).value == Knowledge::unknown);

    // Change only owner context while a real runner read is in flight.
    for (unsigned variant = 0; variant < 5; ++variant) {
        Gate reading, closing;
        Fixture invalidated(true);
        invalidated.synthetic.read_gate = &reading;
        invalidated.synthetic.close_gate = &closing;
        check(invalidated.session.start(invalidated.start(), invalidated.context()));
        reading.await_entry();
        if (variant == 0) { ++invalidated.base.input.observation_generation; invalidated.worker.invalidate(2); }
        if (variant == 1) ++invalidated.base.input.disc_generation;
        if (variant == 2) ++invalidated.base.input.policy_revision;
        if (variant == 3) invalidated.base.input.offset_samples.reset();
        if (variant == 4) invalidated.base.input.device_current = false;
        invalidated.session.poll(invalidated.context());
        check(invalidated.session.busy() && !invalidated.session.released());
        check(invalidated.session.flag(invalidated.context()).value == Knowledge::unknown);
        check(!invalidated.session.start(invalidated.start(), invalidated.context()));
        reading.release(); closing.await_entry();
        invalidated.session.poll(invalidated.context());
        check(invalidated.session.busy() && !invalidated.session.released());
        check(::fcntl(invalidated.synthetic.fd.load(), F_GETFD) >= 0);
        closing.release();
        wait([&] { return invalidated.worker.idle(); });
        MediaWorkerResult stale{};
        check(!invalidated.worker.pop(stale));
        invalidated.session.poll(invalidated.context());
        check(!invalidated.session.busy() && invalidated.session.released());
        check(invalidated.synthetic.reads == 1 && invalidated.synthetic.closes == 1 && invalidated.synthetic.fd_closed);
        const auto response = nlohmann::json::parse(invalidated.session.response(invalidated.context()).body);
        check(response["value"] == "UNKNOWN" && response["source"] == "NONE");
        check(response["state"] != "COMPLETE" && !response["current_context"].get<bool>());
        invalidated.session.poll(invalidated.context());
        check(invalidated.session.flag(invalidated.context()).value == Knowledge::unknown);
    }

    // Invalidate both staged and already published real-run results.
    for (unsigned variant = 0; variant < 3; ++variant) {
        for (bool staged : {false, true}) {
            Fixture invalidated(true);
            check(invalidated.session.start(invalidated.start(), invalidated.context()));
            invalidated.offer();
            if (!staged) invalidated.session.poll(invalidated.context());
            if (variant == 0) { ++invalidated.base.input.observation_generation; invalidated.worker.invalidate(2); }
            if (variant == 1) ++invalidated.base.input.disc_generation;
            if (variant == 2) ++invalidated.base.input.policy_revision;
            check(invalidated.session.flag(invalidated.context()).value == Knowledge::unknown);
            invalidated.session.poll(invalidated.context());
            check(!invalidated.session.busy() && invalidated.session.released());
            check(invalidated.session.flag(invalidated.context()).value == Knowledge::unknown);
            check(nlohmann::json::parse(invalidated.session.response(invalidated.context()).body)["state"] == "INVALIDATED");
            check(invalidated.synthetic.reads == 6 && invalidated.synthetic.closes == 1);
        }
    }
}
}

int main() {
    try {
        integrated_cases();
        Fixture f;
        check(f.session.flag(f.context()).value == Knowledge::unknown);
        for (unsigned variant = 0; variant < 17; ++variant) {
            auto context = f.context();
            switch (variant) {
            case 0: context.direct = false; break;
            case 1: context.c2_supported = false; break;
            case 2: context.input.device_current = false; break;
            case 3: context.input.gate.audio_ready = false; break;
            case 4: context.input.toc = nullptr; break;
            case 5: context.input.disc_generation = 0; break;
            case 6: context.input.observation_generation = 0; break;
            case 7: context.input.gate.playback_stopped = false; break;
            case 8: context.input.gate.output_stopped = false; break;
            case 9: context.input.gate.queue_cancelled = false; break;
            case 10: context.input.gate.playback_reader_released = false; break;
            case 11: context.capture_idle = false; break;
            case 12: context.input.media_idle = false; break;
            case 13: context.input.policy_pending = true; break;
            case 14: context.input.offset_samples.reset(); break;
            case 15: context.input.offset_samples = 1; break;
            case 16: context.input.offset_samples = -1; break;
            }
            check(!f.session.start(f.start(), context) && f.calls == 0 && f.worker.idle());
            check(!f.session.busy() && f.session.released());
        }
        f.base.input.offset_samples.reset();
        check(!f.session.start(f.start(), f.context()) && f.calls == 0);
        f.base.input.offset_samples = 0;
        auto stale = f.start(); stale.observation_generation = 2;
        check(!f.session.start(stale, f.context()) && f.calls == 0);
        auto invalid = f.start();
        auto invalid_ref = std::make_shared<C2Reference>(*invalid.c2_reference);
        invalid_ref->independent = false; invalid.c2_reference = invalid_ref;
        check(f.session.start(invalid, f.context()).failure == ApiCommandFailure::c2_invalid_reference);
        auto outside = f.start();
        auto outside_ref = std::make_shared<C2Reference>(*outside.c2_reference);
        outside_ref->sectors[0].lba = 75; outside.c2_reference = outside_ref;
        check(!f.session.start(outside, f.context()) && f.calls == 0);
        check(f.session.start(f.start(), f.context()));
        check(!f.session.start(f.start(), f.context()));
        f.offer();
        check(f.session.busy() && f.session.released() && f.session.flag(f.context()).value == Knowledge::unknown);
        f.session.poll(f.context());
        check(!f.session.busy() && f.session.flag(f.context()).value == Knowledge::yes);
        f.base.input.gate.playback_stopped = false;
        check(!f.session.interrupt() && f.session.flag(f.context()).value == Knowledge::yes);
        f.base.input.gate.playback_stopped = true;
        check(f.session.cancel(f.cancel(1)) && f.session.flag(f.context()).value == Knowledge::unknown);
        check(f.session.start(f.start(), f.context()));
        f.offer();
        check(f.session.cancel(f.cancel(2)));
        f.session.poll(f.context());
        check(f.session.flag(f.context()).value == Knowledge::unknown && !f.session.busy());
        check(f.session.start(f.start(), f.context()));
        f.offer(); f.session.poll(f.context());
        f.base.input.policy_revision = 4;
        check(f.session.flag(f.context()).value == Knowledge::unknown);
        f.session.poll(f.context());
        check(nlohmann::json::parse(f.session.response(f.context()).body)["state"] == "INVALIDATED");
        Gate blocked;
        Fixture cancelled; cancelled.gate = &blocked;
        check(cancelled.session.start(cancelled.start(), cancelled.context()));
        wait([&] { std::lock_guard lock(blocked.mutex); return blocked.entered; });
        check(cancelled.session.cancel(cancelled.cancel(1)));
        check(cancelled.session.busy() && !cancelled.session.released());
        check(!cancelled.session.start(cancelled.start(), cancelled.context()));
        blocked.release(); wait([&] { return cancelled.worker.idle(); });
        cancelled.session.poll(cancelled.context());
        check(!cancelled.session.busy() && cancelled.session.released());
        check(cancelled.session.flag(cancelled.context()).value == Knowledge::unknown);
        std::cout << "PASS: explicit C2 session admission, staged confirmation, cancel and context invalidation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
