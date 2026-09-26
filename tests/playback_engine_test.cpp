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
class WarningReader final : public CddaReader {
    int position_ = 0;
public:
    void seek(std::int32_t lba) override { position_ = lba; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        const auto frames = pcm.size() / cdda_samples_per_frame;
        ReadResult r{position_, frames, frames, ReadStatus::ok, 0, position_ == 0 ? 1u : 0u};
        std::fill(pcm.begin(), pcm.end(), 0);
        position_ += frames;
        return r;
    }
};

int main() {
    try {
        // A failed output must not publish evidence from the cancelled stream.
        // 出力失敗後に旧streamの再生根拠を残さない。
        {
            PcmWorker worker([] { return std::make_unique<FakeReader>(); });
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 300));
            FakeOutput output;
            PlaybackEngine engine(controller, worker, output, 300);
            controller.play(); engine.synchronize();
            wait_for([&] { return worker.status().done; });
            engine.tick();
            check(engine.read_diagnostics().current_playback.has_value());
            output.fail = true;
            bool failed = false;
            try { engine.tick(); } catch (const std::runtime_error&) { failed = true; }
            check(failed);
            if (engine.read_diagnostics().current_playback)
                throw std::runtime_error("stale current_playback after output failure");
        }
        const PcmBufferConfig defaults;
        check(defaults.capacity_cd_frames == 750 && defaults.startup_cd_frames == 45);
        // ALSA delay need not be available after nonblocking drain starts.
        // drain開始後はdelayを照会せず、末尾のunderrunでは再seekしない。
        for (const bool terminal_error : {false, true}) {
            struct DrainOutput : FakeOutput {
                bool started = false, terminal_error = false;
                std::int64_t delay() override {
                    if (started) throw AudioUnderrun("delay after drain");
                    return FakeOutput::delay();
                }
                bool drain() override {
                    started = true;
                    if (terminal_error) throw AudioUnderrun("terminal drain underrun");
                    return drain_allowed;
                }
            } output;
            output.terminal_error = terminal_error;
            PcmWorker worker([] { return std::make_unique<FakeReader>(); });
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 30));
            PlaybackEngine engine(controller, worker, output, 30);
            controller.play(); engine.synchronize();
            wait_for([&] { return worker.status().done; });
            bool failed = false;
            try { engine.tick(); engine.tick(); engine.tick(); }
            catch (const AudioUnderrun&) { failed = true; }
            check(output.started && failed == terminal_error);
            if (!terminal_error) {
                for (int i = 0; i < 5; ++i) engine.tick();
                output.drain_allowed = true;
                engine.tick();
            }
            check(controller.state().playback == PlaybackState::stopped);
            check(!engine.read_diagnostics().current_playback);
            const auto total = output.total;
            for (int i = 0; i < 5; ++i) engine.tick();
            check(output.total == total);
        }
        // Final write may report XRUN before drain has started.
        // drain前の最終delay失敗でも末尾へ戻らない。
        {
            struct FinalOutput : FakeOutput {
                std::int64_t delay() override {
                    if (total == 588) throw AudioUnderrun("final write underrun");
                    return FakeOutput::delay();
                }
            } output;
            PcmWorker worker([] { return std::make_unique<FakeReader>(); });
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 1));
            PlaybackEngine engine(controller, worker, output, 1);
            controller.play(); engine.synchronize();
            wait_for([&] { return worker.status().done; });
            bool failed = false;
            try { engine.tick(); } catch (const AudioUnderrun&) { failed = true; }
            check(failed && controller.state().playback == PlaybackState::stopped);
            for (int i = 0; i < 5; ++i) engine.tick();
            check(output.total == 588 && !engine.read_diagnostics().current_playback);
        }
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
        // A region need not divide capacity: never wait for a block count
        // that the bounded queue cannot hold (90/75 used to wait forever).
        {
            PcmWorker regional([] { return std::make_unique<FakeReader>(); }, "repeat-test",
                               {90, 90}, {}, "REPEATED", 75);
            check(regional.buffer_capacity_blocks() == 1 && regional.startup_buffer_blocks() == 1);
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 750));
            FakeOutput output;
            PlaybackEngine playback(controller, regional, output, 750);
            controller.play(); playback.synchronize();
            wait_for([&] { return regional.status().queued == 1; });
            playback.tick();
            check(output.total > 0); // Playback progresses before the reader reaches EOF.
            controller.stop(); playback.synchronize();
            regional.reconfigure("single-test", "LEGACY", 15);
            check(regional.startup_buffer_blocks() == 6);
            regional.reconfigure("repeat-test", "REPEATED", 75);
            check(regional.startup_buffer_blocks() == 1);
        }
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
        // A stopped stream can adopt a larger verified-read region.  The
        // already-open reader is discarded, so the next range uses the
        // reader factory and the new immutable generation configuration.
        {
            int readers_created = 0;
            PcmWorker reconfigured([&] {
                ++readers_created;
                return std::make_unique<FakeReader>();
            }, "single-test", {300, 150});
            reconfigured.start(0, 15);
            wait_for([&] { return reconfigured.status().done; });
            check(readers_created == 1);
            reconfigured.reconfigure("repeat-test", "REPEATED", 75);
            check(reconfigured.buffer_capacity_blocks() == 4 &&
                  reconfigured.startup_buffer_blocks() == 2 &&
                  reconfigured.read_block_cd_frames() == 75);
            reconfigured.start(100, 475);
            wait_for([&] { return reconfigured.status().queued == 4; });
            PcmBlock block;
            check(reconfigured.pop(block));
            check(block.lba == 100 && block.evidence.frames_requested == 75 &&
                  block.samples.size() == 75 * cdda_samples_per_frame);
            check(readers_created == 2);
            check(block.evidence.policy_revision == 2);
            check(block.evidence.device_generation == 2);
            check(block.evidence.stream_generation == block.generation);
            check(reconfigured.status().diagnostics.effective_strategy == "repeat-test");
            reconfigured.cancel();
        }
        {
            PcmWorker warnings([] { return std::make_unique<WarningReader>(); });
            warnings.start(0, 30);
            wait_for([&] { return warnings.status().done; });
            const auto state = warnings.status().diagnostics;
            check(state.latest->status == IntegrityReadStatus::clean);
            check(state.active_warning && state.active_warning->start_lba == 0);
            PlayerEvent ignored;
            while (warnings.pop_event(ignored)) {}
            check(warnings.status().diagnostics.active_warning.has_value());
            warnings.cancel();
            check(!warnings.status().diagnostics.active_warning);
        }
        // Bounded history is independent of event draining and PCM consumption.
        {
            PcmWorker history([] { return std::make_unique<FakeReader>(); });
            history.set_disc_generation(7);
            const auto generation = history.start(0, 15 * 140);
            PcmBlock block;
            for (int i = 0; i < 140; ++i) {
                wait_for([&] { return history.pop(block); });
                check(block.evidence.disc_generation == 7);
                check(block.evidence.device_generation == 1);
                check(block.evidence.stream_generation == generation);
            }
            wait_for([&] { return history.status().done; });
            const auto snapshot = history.status(true).diagnostics;
            check(snapshot.recent_reads.size() == 128 && snapshot.history_evicted == 12);
            check(snapshot.recent_reads.front().read_sequence == 13);
            check(snapshot.recent_reads.back().read_sequence == 140);
            check(history.status().diagnostics.recent_reads.empty());
            check(snapshot.disc_map && snapshot.disc_map->size == 1);
            check(snapshot.disc_map->regions[0].end == 2100);
            check(!history.status().diagnostics.disc_map);
            history.cancel();
            check(history.status(true).diagnostics.disc_map->regions[0].end == 2100);
            check(history.status(true).diagnostics.recent_reads.empty());
            history.set_disc_generation(8);
            check(history.status(true).diagnostics.disc_map->size == 0);
            history.start(0, 15);
            wait_for([&] { return history.status().done; });
            check(history.status(true).diagnostics.recent_reads.front().disc_generation == 8);
        }
        // Pause diagnostic consumption, not PCM: overflow must never stall reads.
        // 診断consumerだけを止め、PCM進行と破棄順序・世代境界を確認する。
        for (const bool discard : {false, true}) {
            PcmWorker overflow([] { return std::make_unique<FakeReader>(); });
            overflow.set_disc_generation(20);
            const auto generation = overflow.start(0, 15 * 300);
            PcmBlock block;
            ReadDiagnostics retained;
            for (unsigned i = 1; i <= 300; ++i) {
                wait_for([&] { return overflow.pop(block); });
                check(block.evidence.read_sequence == i);
                check(block.generation == generation);
                check(block.evidence.disc_generation == 20);
                const auto ordinary = overflow.status();
                check(ordinary.queued <= overflow.buffer_capacity_blocks());
                check(ordinary.diagnostics.recent_reads.empty());
                if (i == 1) retained = overflow.status(true).diagnostics;
            }
            wait_for([&] { return overflow.status().done; });
            const auto detailed = overflow.status(true).diagnostics;
            check(detailed.stats.read_calls == 300 && detailed.dropped_events == 44);
            check(detailed.history_evicted == 172 && detailed.recent_reads.size() == 128);
            check(detailed.recent_reads.front().read_sequence == 173);
            check(detailed.recent_reads.back().read_sequence == 300);
            check(detailed.coverage.accepted_unique_frames == 4500 && detailed.coverage.complete);
            // A consumer may hold its copied snapshot indefinitely without pinning the worker.
            // 取得済みsnapshotの保持はworker側の保存領域を固定しない。
            check(retained.recent_reads.front().read_sequence == 1);
            check(retained.recent_reads.size() <= read_history_capacity);
            PlayerEvent event;
            for (unsigned sequence = 45; sequence <= 300; ++sequence) {
                check(overflow.pop_event(event));
                check(event.read.read_sequence == sequence && event.stream_generation == generation);
            }
            check(!overflow.pop_event(event));
            if (discard) overflow.discard_reader(); else overflow.cancel();
            if (overflow.status().diagnostics.dropped_events != 0)
                throw std::runtime_error("old dropped_events after cancel");
            overflow.set_disc_generation(21);
            const auto next = overflow.start(9000, 9015);
            wait_for([&] { return overflow.status().done; });
            const auto reset = overflow.status(true).diagnostics;
            check(next != generation && reset.dropped_events == 0 && reset.history_evicted == 0);
            check(reset.recent_reads.size() == 1 && reset.recent_reads[0].read_sequence == 1);
            check(reset.recent_reads[0].disc_generation == 21 && reset.recent_reads[0].start_lba == 9000);
            check(reset.coverage.accepted_unique_frames == 15);
            check(overflow.pop_event(event) && event.stream_generation == next && !overflow.pop_event(event));
        }
        // Max supported prefetch can evict evidence before its PCM reaches output.
        // 最大先読みで履歴から消えても、engineの再生根拠はPCMから復元される。
        {
            PcmWorker prefetched([] { return std::make_unique<FakeReader>(); }, "test", {2250, 15});
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 2250));
            FakeOutput output;
            PlaybackEngine playback(controller, prefetched, output, 2250);
            controller.play(); playback.synchronize();
            wait_for([&] { return prefetched.status().done; });
            const auto history = playback.read_diagnostics(true);
            check(history.history_evicted == 22 && history.recent_reads.front().read_sequence == 23);
            playback.tick();
            check(output.total > 0);
            const auto current = playback.read_diagnostics();
            check(current.current_playback && current.current_playback->read_sequence == 1);
            check(current.current_playback->start_lba == 0 && current.latest->read_sequence == 150);
            check(current.recent_reads.empty());
            output.pending = 0;
            playback.tick();
            check(output.total > 12 * 588);
            check(playback.read_diagnostics().current_playback->start_lba == 0);
            controller.stop(); playback.synchronize();
            check(!playback.read_diagnostics().current_playback);
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
        for (const bool new_disc : {false, true}) {
            auto gate = std::make_shared<Gate>();
            PcmWorker w([gate] { return std::make_unique<FakeReader>(gate); });
            w.set_disc_generation(7);
            w.start(0, 15);
            {
                std::unique_lock lock(gate->mutex);
                if (!gate->cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate->entered; })) {
                    gate->release=true; gate->cv.notify_all(); throw std::runtime_error("reader did not enter");
                }
            }
            w.cancel();
            if (new_disc) w.set_disc_generation(8);
            const auto current = w.start(100, 115);
            { std::lock_guard lock(gate->mutex); gate->release = true; }
            gate->cv.notify_all();
            wait_for([&] { return w.status().done; });
            PcmBlock b;
            check(w.pop(b) && b.generation == current && b.lba == 100 && b.samples[0] == 100);
            check(b.evidence.start_lba == 100 && b.evidence.frames_read == 15);
            check(b.evidence.local_verification == LocalVerification::single_read);
            const auto map = w.status(true).diagnostics.disc_map;
            check(map && map->disc_generation == (new_disc ? 8 : 7));
            check(map->size == (new_disc ? 1 : 2));
            if (new_disc) check(map->regions[0].begin == 100);
            check(w.status().diagnostics.stats.read_calls == 1); // Superseded read is not current evidence.
            PlayerEvent event;
            check(w.pop_event(event) && event.stream_generation == current);
            check(event.read.start_lba == 100 && !w.pop_event(event));
            check(w.status().diagnostics.coverage.accepted_unique_frames == 15);
            check(w.status().diagnostics.stream_generation == current);
            check(b.evidence.stream_generation == current);
            w.cancel();
            check(w.status().diagnostics.coverage.accepted_unique_frames == 0);
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
