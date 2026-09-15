#include "playback_engine.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool ok) { if (!ok) throw std::runtime_error("playback test failed"); }
template<class F> void wait_for(F condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("test timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Gate { std::mutex mutex; std::condition_variable cv; bool entered=false, release=false; };
class FakeReader : public CddaReader {
    std::int32_t position_ = 0;
    std::shared_ptr<Gate> gate_;
public:
    explicit FakeReader(std::shared_ptr<Gate> gate = {}) : gate_(gate) {}
    void seek(std::int32_t p) override { position_ = p; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        if (gate_) {
            std::unique_lock lock(gate_->mutex);
            gate_->entered = true; gate_->cv.notify_all();
            gate_->cv.wait(lock, [&] { return gate_->release; });
        }
        const auto frames = pcm.size() / cdda_samples_per_frame;
        std::fill(pcm.begin(), pcm.end(), static_cast<std::int16_t>(position_));
        ReadResult r{position_, frames, frames, ReadStatus::ok, 0, 0};
        position_ += static_cast<std::int32_t>(frames); return r;
    }
};
struct FakeOutput : AudioOutput {
    std::int64_t pending = 0;
    std::size_t total = 0;
    bool blocked = false, fail = false, drain_allowed = false;
    int resets = 0;
    void reset() override { ++resets; pending = 0; }
    std::size_t write(std::span<const std::int16_t> samples) override {
        if (fail) throw std::runtime_error("simulated underrun");
        if (blocked) return 0;
        const auto accepted = std::min<std::size_t>(588, samples.size()/2);
        pending += accepted; total += accepted; return accepted;
    }
    std::int64_t delay() override { return pending; }
    bool drain() override { return drain_allowed; }
};
int main() {
    try {
        // An in-flight old read must not publish after a new range is requested.
        auto gate = std::make_shared<Gate>();
        {
            PcmWorker w([gate] { return std::make_unique<FakeReader>(gate); });
            w.start(0, 15);
            {
                std::unique_lock lock(gate->mutex);
                if (!gate->cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate->entered; })) {
                    gate->release=true; gate->cv.notify_all(); throw std::runtime_error("reader did not enter");
                }
            }
            w.cancel();
            const auto current = w.start(100, 115);
            { std::lock_guard lock(gate->mutex); gate->release = true; }
            gate->cv.notify_all();
            wait_for([&] { return w.status().done; });
            PcmBlock b;
            check(w.pop(b) && b.generation == current && b.lba == 100 && b.samples[0] == 100);
            check(!w.pop(b));
            w.start(0, 1000);
            wait_for([&] { return w.status().queued == 10; });
            check(!w.status().done); w.cancel();
            check(w.status().queued == 0);
        }
        PlayerController c;
        c.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0,15}, 30));
        PcmWorker w([] { return std::make_unique<FakeReader>(); });
        FakeOutput a;
        PlaybackEngine engine(c,w,a,30);
        c.play(); engine.synchronize();
        wait_for([&] { return w.status().done; });
        engine.tick();
        check(c.state().position_lba == 0); // queued to ALSA is not yet played
        check(a.total == 12 * 588); // partial writes retained across ticks
        a.pending = 0; a.blocked = true;
        engine.tick(); check(c.state().position_lba == 12);
        c.pause(); engine.synchronize();
        check(w.status().queued == 0 && a.pending == 0);
        check(c.state().playback == PlaybackState::paused);
        c.select_track(2); c.play(); engine.synchronize();
        wait_for([&] { return w.status().done; });
        a.blocked = false; a.drain_allowed = true;
        engine.tick(); engine.tick();
        check(c.state().playback == PlaybackState::stopped && c.state().track == 1);
        c.play(); engine.synchronize();
        wait_for([&] { return w.status().done; });
        a.fail = true;
        bool failed = false;
        try { engine.tick(); } catch (const std::runtime_error&) { failed = true; }
        check(failed && c.state().playback == PlaybackState::stopped && w.status().queued == 0);
        std::cout << "PASS: generation cancellation, bounded queue, partial writes, position, pause, finish, error\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
