#include "complete_pcm_session.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace {
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
    CompletePcmSession session{"capture-test", "direct", [this] { return std::make_unique<Reader>(shared); },
        [this] { ++shared->probes; return toc; }, std::make_shared<DriveAccessCoordinator>()};
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
