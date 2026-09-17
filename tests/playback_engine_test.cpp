#include "playback_engine.hpp"
#include <algorithm>
#include <atomic>
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
    bool blocked = false, fail = false, underrun_delay = false, drain_allowed = false;
    int resets = 0;
    void reset() override { ++resets; pending = 0; }
    std::size_t write(std::span<const std::int16_t> samples) override {
        if (fail) throw std::runtime_error("simulated output failure");
        if (blocked) return 0;
        const auto accepted = std::min<std::size_t>(588, samples.size()/2);
        pending += accepted; total += accepted; return accepted;
    }
    std::int64_t delay() override {
        if (underrun_delay) throw AudioUnderrun("simulated ALSA delay underrun");
        return pending;
    }
    bool drain() override { return drain_allowed; }
};
int main() {
    try {
        // Removal must release the reader even if no further Play arrives.
        std::atomic<int> destroyed{0};
        struct ClosingReader : FakeReader {
            std::atomic<int>& count;
            explicit ClosingReader(std::atomic<int>& count) : count(count) {}
            ~ClosingReader() override { ++count; }
        };
        {
            PcmWorker idle([&] { return std::make_unique<ClosingReader>(destroyed); });
            idle.start(0, 15);
            wait_for([&] { return idle.status().done; });
            idle.cancel();
            idle.discard_reader();
            wait_for([&] { return destroyed.load() == 1 && idle.device_released(); });
            idle.start(0, 15);
            wait_for([&] { return idle.status().done; });
        }
        check(destroyed == 2);
        // A cancelled slow open is still a live device operation until it
        // returns and the newly-created reader has been discarded.
        {
            std::mutex open_mutex;
            std::condition_variable open_changed;
            bool open_entered = false, release_open = false;
            PcmWorker opening([&] {
                std::unique_lock lock(open_mutex);
                open_entered = true; open_changed.notify_all();
                open_changed.wait(lock, [&] { return release_open; });
                return std::make_unique<FakeReader>();
            });
            opening.start(0, 15);
            {
                std::unique_lock lock(open_mutex);
                open_changed.wait(lock, [&] { return open_entered; });
            }
            opening.cancel();
            check(!opening.device_released());
            opening.discard_reader();
            { std::lock_guard lock(open_mutex); release_open = true; }
            open_changed.notify_all();
            wait_for([&] { return opening.device_released(); });
        }
        bool invalid_buffer = false;
        try {
            PcmWorker invalid([] { return std::make_unique<FakeReader>(); }, "test", {14, 15});
        } catch (const std::invalid_argument&) { invalid_buffer = true; }
        check(invalid_buffer);
        bool invalid_read_block = false;
        try {
            PcmWorker invalid([] { return std::make_unique<FakeReader>(); }, "test", {30, 15},
                              {}, "REPEATED", 75);
        } catch (const std::invalid_argument&) { invalid_read_block = true; }
        check(invalid_read_block);
        {
            PcmWorker bounded([] { return std::make_unique<FakeReader>(); }, "test", {30, 15});
            check(bounded.buffer_capacity_blocks() == 2 && bounded.startup_buffer_blocks() == 1);
            bounded.start(0, 1000);
            wait_for([&] { return bounded.status().queued == 2; });
            const auto diagnostics = bounded.status().diagnostics;
            check(diagnostics.buffer_capacity_frames == 30 && diagnostics.startup_buffer_frames == 15);
            check(diagnostics.read_block_frames == 15);
            bounded.cancel();
        }
        {
            PcmWorker regional([] { return std::make_unique<FakeReader>(); }, "repeat-test",
                               {300, 150}, {}, "REPEATED", 75);
            check(regional.buffer_capacity_blocks() == 4 && regional.startup_buffer_blocks() == 2);
            check(regional.status().diagnostics.read_block_frames == 75);
            regional.start(0, 375);
            wait_for([&] { return regional.status().queued == 4; });
            PcmBlock block;
            check(regional.pop(block));
            check(block.evidence.frames_requested == 75 && block.samples.size() == 75 * cdda_samples_per_frame);
            regional.cancel();
        }
        // Media work using the same coordinator cannot overlap a PCM read.
        {
            auto coordinated_gate = std::make_shared<Gate>();
            auto access = std::make_shared<DriveAccessCoordinator>();
            PcmWorker coordinated([coordinated_gate] {
                return std::make_unique<FakeReader>(coordinated_gate);
            }, "test", {}, access);
            coordinated.start(0, 15);
            {
                std::unique_lock lock(coordinated_gate->mutex);
                if (!coordinated_gate->cv.wait_for(lock, std::chrono::seconds(3),
                                                   [&] { return coordinated_gate->entered; }))
                    throw std::runtime_error("coordinated reader did not enter");
            }
            std::atomic<bool> media_attempting = false, media_entered = false;
            std::thread media([&] {
                media_attempting = true;
                access->invoke([&] { media_entered = true; });
            });
            wait_for([&] { return media_attempting.load(); });
            check(!media_entered.load());
            { std::lock_guard lock(coordinated_gate->mutex); coordinated_gate->release = true; }
            coordinated_gate->cv.notify_all();
            media.join();
            check(media_entered.load());
            wait_for([&] { return coordinated.status().done; });
        }
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
            check(b.evidence.start_lba == 100 && b.evidence.frames_read == 15);
            check(b.evidence.local_verification == LocalVerification::single_read);
            check(w.status().diagnostics.stats.read_calls == 1); // Superseded read is not current evidence.
            PlayerEvent event;
            check(w.pop_event(event) && event.stream_generation == current);
            check(event.read.start_lba == 100 && !w.pop_event(event));
            check(!w.pop(b));
            w.start(0, 1000);
            wait_for([&] { return w.status().queued == pcm_queue_capacity_blocks; });
            check(!w.status().done); w.cancel();
            check(w.status().queued == 0);
        }
        PlayerController c;
        c.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0,15}, 30));
        int readers_created = 0;
        PcmWorker w([&] { ++readers_created; return std::make_unique<FakeReader>(); });
        FakeOutput a;
        PlaybackEngine engine(c,w,a,0); // Player sessions start before a disc is ready.
        engine.set_disc_end(30);
        bool invalid_end = false;
        try { engine.set_disc_end(-1); } catch (const std::invalid_argument&) { invalid_end = true; }
        check(invalid_end);
        c.play(); engine.synchronize();
        wait_for([&] { return w.status().done; });
        engine.tick();
        check(c.state().position_lba == 0); // queued to ALSA is not yet played
        check(engine.read_diagnostics().current_playback.has_value());
        check(a.total == 12 * 588); // partial writes retained across ticks
        a.pending = 0; a.blocked = true;
        engine.tick(); check(c.state().position_lba == 12);
        check(engine.read_diagnostics().current_playback->start_lba == 0);
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
        a.fail = false;
        c.play(); engine.synchronize();
        wait_for([&] { return w.status().done; });
        check(readers_created == 2); // Playback error invalidated the old device handle.
        a.underrun_delay = true;
        engine.tick();
        check(c.state().playback == PlaybackState::playing);
        a.underrun_delay = false;
        wait_for([&] { return w.status().done; });
        engine.tick(); engine.tick(); engine.tick();
        check(readers_created == 3 && c.state().playback == PlaybackState::stopped);
        std::cout << "PASS: generation cancellation, bounded queue, partial writes, position, pause, finish, error\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
