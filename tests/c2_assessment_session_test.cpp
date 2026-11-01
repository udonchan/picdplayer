#include "c2_assessment_session.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
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
    ~Gate() { release(); }
};
struct Fixture {
    DiscToc toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
    std::atomic<unsigned> calls{0};
    Gate* gate = nullptr;
    MediaWorker worker;
    C2AssessmentSession session;
    C2AssessmentContext base;
    Fixture() : worker(std::make_shared<DriveAccessCoordinator>(), [] { return MediaObservation::audio_disc; },
        [this] { return toc; }, [] {}, {}, {}, {}, {}, {},
        [this](const C2Reference&, const auto& current) {
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
    ~Fixture() { if (gate) gate->release(); (void)session.interrupt(); }
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
}

int main() {
    try {
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
