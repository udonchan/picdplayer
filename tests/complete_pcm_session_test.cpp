#include "complete_pcm_session.hpp"
#include "media_worker.hpp"
#include "player_media_dispatch.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
struct OwnerClock {
    using time_point = std::chrono::steady_clock::time_point;
    static inline time_point current{};
    static time_point now() { return current; }
};
using Json = nlohmann::json;
unsigned checks = 0;
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("capture session assertion " + std::to_string(checks));
}
struct Shared {
    std::atomic<unsigned> opens{0}, closes{0}, probes{0};
    std::mutex mutex;
    std::condition_variable changed;
    bool block = false, entered = false;
    void release() { std::lock_guard lock(mutex); block = false; changed.notify_all(); }
};
struct ReleaseShared {
    Shared& shared;
    ~ReleaseShared() { shared.release(); }
};
class Reader final : public CddaReader {
    std::shared_ptr<Shared> shared_;
    std::int32_t position_ = 0;
public:
    explicit Reader(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) { ++shared_->opens; }
    ~Reader() override { ++shared_->closes; }
    void seek(std::int32_t lba) override { position_ = lba; }
    ReadResult read(std::span<std::int16_t> output) override {
        {
            std::unique_lock lock(shared_->mutex);
            shared_->entered = true; shared_->changed.notify_all();
            if (!shared_->changed.wait_for(lock, std::chrono::seconds(3), [&] { return !shared_->block; }))
                throw std::runtime_error("fake read gate timeout");
        }
        const auto frames = output.size() / cdda_samples_per_frame;
        for (std::size_t i = 0; i < output.size(); ++i) output[i] = static_cast<std::int16_t>((position_ + i) % 30000);
        ReadResult result{position_, frames, frames, ReadStatus::ok, 0, 0};
        position_ += static_cast<std::int32_t>(frames);
        return result;
    }
};
struct Fixture {
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    DiscToc toc = make_audio_toc(7, std::vector<std::int32_t>{150,151}, 152);
    std::shared_ptr<DriveAccessCoordinator> access = std::make_shared<DriveAccessCoordinator>();
    CompletePcmSession session{"capture-test", "direct", [this] { return std::make_unique<Reader>(shared); },
        [this] { ++shared->probes; return toc; }, access};
    ~Fixture() { shared->release(); }
    CompletePcmContext context() {
        return {{true,true,true,true,true}, &toc, true, true, false, 2, 3, 4, 0, ReadPolicy{}};
    }
    ApiCommand command() {
        ApiCommand command{}; command.type = ApiCommandType::start_complete_pcm;
        command.session_id = "capture-test"; command.disc_generation = 2; command.duration_ms = 2000;
        return command;
    }
    Json status(const CompletePcmContext& c) { return Json::parse(session.response(c, false).body); }
    void wait(const CompletePcmContext& c, const std::function<bool(const Json&)>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            session.poll(c);
            if (done(status(c))) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("capture session deadline");
    }
};
}

int main() {
    try {
        {
            Fixture f;
            const auto ready = f.context();
            check(f.status(ready)["state"] == "IDLE" && f.status(ready)["can_start"] == true);
            check(f.shared->opens == 0 && f.shared->probes == 0);
            auto reject = [&](CompletePcmContext c) {
                check(f.session.start_rejection(c) != nullptr);
                check(!f.session.start(f.command(), c));
                check(f.shared->opens == 0 && f.shared->probes == 0);
            };
            auto c = ready; c.gate.playback_stopped = false; reject(c);
            c = ready; c.gate.output_stopped = false; reject(c);
            c = ready; c.gate.queue_cancelled = false; reject(c);
            c = ready; c.gate.playback_reader_released = false; reject(c);
            c = ready; c.offset_samples.reset(); reject(c);
            c = ready; c.offset_samples = 1; reject(c);
            c = ready; c.media_idle = false; reject(c);
            c = ready; c.device_current = false; reject(c);
            c = ready; c.policy_pending = true; reject(c);
            c = ready; c.policy.region_frames = 150; reject(c);
            check(std::string(f.session.start_rejection(c)) == "policy_budget_unsupported");
            auto command = f.command(); command.session_id = "old";
            check(!f.session.start(command, ready));
            check(f.shared->opens == 0 && f.shared->probes == 0);
            check(f.session.start(f.command(), ready).accepted);
            f.wait(ready, [](const auto& state) { return state["state"] == "COMPLETE"; });
            check(f.shared->opens == f.shared->closes && f.shared->opens == 1 && f.shared->probes == 2);
            auto response = f.session.response(ready, true);
            check(response.status == 200 && response.body.size() <= complete_pcm_result_limit);
            const auto result = Json::parse(response.body);
            check(result["kind"] == "complete_pcm_manifest" && result["reader_released"] == true);
            check(result["duration_ms"] == 2000 && f.status(ready)["duration_ms"] == 2000);
            check(result["identity"]["device_observation_generation"] == 3);
            check(result["identity"]["reader_incarnation"] == 1 && result["identity"]["policy_revision"] == 4);
            check(result["coverage"]["frames"] == 2 && result["coverage"]["bytes"] == 4704);
            check(result["fingerprint"]["tracks"].size() == 2 && result["fingerprint"]["sha256"].get<std::string>().size() == 64);
            check(result["external_verification"] == "NOT_CHECKED" && result["cache_independence"] == "NOT_PROVEN");
            c = ready; ++c.observation_generation;
            check(f.session.response(c, true).status == 404);
            f.session.poll(c);
            check(f.status(c)["state"] == "INVALID");
        }
        {
            Fixture f;
            std::atomic<bool> media_device_open{false};
            { std::lock_guard lock(f.shared->mutex); f.shared->block = true; }
            MediaWorker media(f.access, [&] {
                media_device_open = true;
                {
                    std::unique_lock lock(f.shared->mutex);
                    f.shared->entered = true;
                    f.shared->changed.notify_all();
                    if (!f.shared->changed.wait_for(lock, std::chrono::seconds(3), [&] { return !f.shared->block; }))
                        throw std::runtime_error("fake media gate timeout");
                }
                media_device_open = false;
                return MediaObservation::audio_disc;
            }, [toc = f.toc] { return toc; }, [] {});
            ReleaseShared release{*f.shared};
            auto c = f.context();
            media.invalidate(c.observation_generation);
            check(media.request(MediaWork::observe));
            {
                std::unique_lock lock(f.shared->mutex);
                check(f.shared->changed.wait_for(lock, std::chrono::seconds(2), [&] { return f.shared->entered; }));
            }
            MediaWorkerResult result{};
            check(!media.pop(result)); // Owner drained before the observation completed.
            f.shared->release();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (media.activity() != "RESULT_PENDING" && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(media.activity() == "RESULT_PENDING" && !media_device_open);
            c.media_activity = media.activity();
            c.media_idle = c.media_activity == "IDLE";
            check(f.status(c)["media_activity"] == "RESULT_PENDING" && f.status(c)["start_rejection"] == "media_busy");
            const auto rejected = f.session.start(f.command(), c);
            check(!rejected.accepted && rejected.failure == ApiCommandFailure::capture_not_ready);
            check(f.shared->opens == 0 && f.shared->probes == 0 && f.status(c)["job_id"] == 0);
            check(media.pop(result) && result.observation == MediaObservation::audio_disc &&
                  result.observation_generation == c.observation_generation);
            c.media_activity = media.activity();
            c.media_idle = c.media_activity == "IDLE";
            f.session.poll(c);
            check(f.status(c)["media_activity"] == "IDLE" && f.status(c)["can_start"] == true);
            check(f.shared->opens == 0 && f.shared->probes == 0 && f.status(c)["job_id"] == 0);
            check(f.session.start(f.command(), c).accepted);
            f.wait(c, [](const auto& state) { return state["state"] == "COMPLETE"; });
            check(f.status(c)["job_id"] == 1 && f.shared->opens == 1 && f.shared->closes == 1);
        }
        {
            Fixture f;
            auto c = f.context();
            c.media_activity = "IDLE";
            std::atomic<int> observes{0};
            MediaWorker media(f.access, [&] { ++observes; return MediaObservation::audio_disc; },
                              [toc = f.toc] { return toc; }, [] {});
            ReleaseShared release{*f.shared};
            { std::lock_guard lock(f.shared->mutex); f.shared->block = true; }
            auto refresh = [&] { c.media_activity = media.activity(); c.media_idle = c.media_activity == "IDLE"; };
            auto can_poll = [&] { return !f.session.busy() && f.session.released(); };
            OwnerClock::current = {};
            auto next_media = OwnerClock::now();
            ApiCommandResult admitted{false};
            service_commands_before_media_poll<OwnerClock>(media, next_media, [&] {
                refresh();
                admitted = f.session.start(f.command(), c);
                f.session.poll(c);
            }, can_poll);
            check(admitted.accepted && f.session.busy() && !f.session.released());
            check(media.idle() && observes == 0); // No periodic request may even be queued after admission.
            {
                std::unique_lock lock(f.shared->mutex);
                check(f.shared->changed.wait_for(lock, std::chrono::seconds(2), [&] { return f.shared->entered; }));
            }
            OwnerClock::current = next_media;
            ApiCommandResult cancelled{false};
            service_commands_before_media_poll<OwnerClock>(media, next_media, [&] {
                refresh();
                auto cancel = f.command();
                cancel.type = ApiCommandType::cancel_complete_pcm;
                cancel.job_id = 1;
                cancelled = f.session.cancel(cancel);
                f.session.poll(c);
            }, can_poll);
            check(cancelled.accepted && f.session.busy() && !f.session.released());
            check(media.idle() && observes == 0);
            f.shared->release();
            f.wait(c, [](const auto& state) { return !state["busy"].template get<bool>(); });
            check(f.session.released() && f.shared->closes == 1);
            OwnerClock::current = next_media;
            service_commands_before_media_poll<OwnerClock>(media, next_media,
                [&] { refresh(); f.session.poll(c); }, can_poll);
            check(!media.idle());
            MediaWorkerResult result{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool popped = false;
            while (!(popped = media.pop(result)) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(popped && media.idle() && observes == 1 && result.work == MediaWork::observe);
            check(f.shared->opens == 1 && f.status(c)["job_id"] == 1);
        }
        {
            Fixture f;
            std::atomic<int> observes{0};
            MediaWorker media(f.access, [&] { ++observes; return MediaObservation::no_disc; },
                              [toc = f.toc] { return toc; }, [] {});
            auto c = f.context();
            media.invalidate(c.observation_generation);
            check(media.request(MediaWork::observe));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (media.activity() != "RESULT_PENDING" && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(media.activity() == "RESULT_PENDING");
            bool ready = true;
            int attempts = 0;
            ApiCommandResult rejected{false};
            auto service = [&] {
                c.media_activity = media.activity(); c.media_idle = c.media_activity == "IDLE";
                if (ready) { ready = false; ++attempts; rejected = f.session.start(f.command(), c); }
            };
            OwnerClock::current = {};
            auto next_media = OwnerClock::now();
            service_commands_before_media_poll<OwnerClock>(media, next_media, service, [] { return true; });
            check(!rejected.accepted && f.shared->opens == 0 && f.shared->probes == 0);
            MediaWorkerResult result{};
            check(media.pop(result) && result.observation == MediaObservation::no_disc);
            c.gate.audio_ready = false; c.toc = nullptr; // Apply delivered media meaning before servicing commands.
            service_commands_before_media_poll<OwnerClock>(media, next_media, service, [] { return true; });
            check(attempts == 1 && f.shared->opens == 0 && f.status(c)["job_id"] == 0);
            ready = true;
            service_commands_before_media_poll<OwnerClock>(media, next_media, service, [] { return true; });
            check(attempts == 2 && !rejected.accepted && f.status(c)["start_rejection"] == "audio_not_ready");
            check(f.shared->opens == 0 && f.shared->probes == 0 && observes == 1);
        }
        {
            Fixture f;
            auto c = f.context();
            c.media_activity = "IDLE";
            { std::lock_guard lock(f.shared->mutex); f.shared->block = true; }
            check(f.session.start(f.command(), c).accepted);
            {
                std::unique_lock lock(f.shared->mutex);
                check(f.shared->changed.wait_for(lock, std::chrono::seconds(2), [&] { return f.shared->entered; }));
            }
            c.media_idle = false;
            c.media_activity = "RESULT_PENDING";
            f.session.poll(c);
            check(f.session.busy() && !f.session.released());
            check(f.status(c)["media_activity"] == "RESULT_PENDING" && f.status(c)["start_rejection"] == "capture_releasing");
            check(!f.session.start(f.command(), c) && f.shared->opens == 1);
            check(f.session.response(c, true).status == 404);
            f.shared->release();
            f.wait(c, [](const auto& state) { return !state["busy"].template get<bool>(); });
            check(f.session.released() && f.shared->closes == 1 && f.session.response(c, true).status == 404);
            c.media_idle = true;
            c.media_activity = "IDLE";
            f.session.poll(c);
            check(f.status(c)["state"] == "INVALID" && f.status(c)["job_id"] == 1);
            check(f.shared->opens == 1 && f.session.response(c, true).status == 404);
        }
        {
            Fixture f;
            const auto c = f.context();
            { std::lock_guard lock(f.shared->mutex); f.shared->block = true; }
            check(f.session.start(f.command(), c).accepted);
            {
                std::unique_lock lock(f.shared->mutex);
                check(f.shared->changed.wait_for(lock, std::chrono::seconds(2), [&] { return f.shared->entered; }));
            }
            check(f.session.response(c, true).status == 404);
            auto wrong = f.command(); wrong.type = ApiCommandType::cancel_complete_pcm; wrong.job_id = 99;
            check(!f.session.cancel(wrong));
            const auto begin = std::chrono::steady_clock::now();
            check(f.session.interrupt());
            check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(50));
            check(f.session.busy() && !f.session.released());
            check(!f.session.start(f.command(), c));
            check(f.shared->opens == 1);
            f.shared->release();
            f.wait(c, [](const auto& state) { return !state["busy"].template get<bool>(); });
            check(f.session.released() && f.session.response(c, true).status == 404);
            check(f.session.start(f.command(), c).accepted);
            f.wait(c, [](const auto& state) { return state["state"] == "COMPLETE"; });
            check(f.status(c)["job_id"] == 2);
            auto cancel = f.command(); cancel.type = ApiCommandType::cancel_complete_pcm; cancel.job_id = 2;
            check(f.session.cancel(cancel).accepted);
            check(f.session.response(c, true).status == 404);
        }
        std::cout << "PASS: production capture session gates, receipt, cancellation/release (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
