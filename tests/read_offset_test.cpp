#include "read_offset.hpp"
#include "integrity_state.hpp"
#include "playback_engine.hpp"
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

void check(bool ok) { if (!ok) throw std::runtime_error("read offset test failed"); }
std::int16_t sample_at(std::int64_t frame, unsigned channel) {
    return static_cast<std::int16_t>((frame * 37 + channel * 997) % 60013 - 30000);
}
struct RawState {
    std::int32_t begin = 0, end = 300;
    std::size_t calls = 0;
    bool partial = false, wrong_start = false;
};
class SampleReader final : public CddaReader {
    std::shared_ptr<RawState> state_;
    std::int32_t position_ = 0;
public:
    explicit SampleReader(std::shared_ptr<RawState> state) : state_(std::move(state)) {}
    void seek(std::int32_t lba) override { position_ = lba; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        const auto frames = pcm.size() / cdda_samples_per_frame;
        check(position_ >= state_->begin && position_ + static_cast<std::int64_t>(frames) <= state_->end);
        ++state_->calls;
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = sample_at(std::int64_t(position_) * 588 + i / 2, i % 2);
        ReadResult result{position_, frames, frames, ReadStatus::ok, 0, 0};
        if (state_->partial) { --result.frames_read; result.retries = 1; result.c2_status = C2Status::reported; }
        if (state_->wrong_start) ++result.start_lba;
        position_ += static_cast<std::int32_t>(frames);
        return result;
    }
};
int main() {
    try {
        for (int begin : {0, 150}) for (int offset : {-5000, -589, -588, -587, -1, 0, 1, 587, 588, 589, 5000}) {
            auto state = std::make_shared<RawState>(); state->begin = begin; state->end = begin + 300;
            const auto window = make_read_offset_window(begin, begin + 300, offset);
            auto reader = make_offset_reader(std::make_unique<SampleReader>(state), window);
            reader->seek(window.playable_begin_lba);
            for (auto p = window.playable_begin_lba; p < window.playable_end_lba;) {
                const auto n = std::min(15, window.playable_end_lba - p);
                std::vector<std::int16_t> pcm(n * cdda_samples_per_frame);
                const auto result = reader->read(pcm);
                check(result.status == ReadStatus::ok && result.start_lba == p && result.frames_read == std::size_t(n));
                check(result.offset && result.offset->samples == offset && result.offset->single_physical_read);
                const auto& raw = *result.offset->single_physical_read;
                check(raw.frames_requested == std::size_t(n + (offset % 588 != 0)));
                check(make_read_evidence(result).offset_status == OffsetStatus::corrected);
                for (std::size_t i = 0; i < pcm.size(); ++i)
                    check(pcm[i] == sample_at(std::int64_t(p) * 588 + offset + i / 2, i % 2));
                p += n;
            }
            const auto calls = state->calls;
            std::vector<std::int16_t> pcm(cdda_samples_per_frame, 1234);
            check(reader->read(pcm).status == ReadStatus::read_error && state->calls == calls);
            check(std::all_of(pcm.begin(), pcm.end(), [](auto value) { return value == 1234; }));
        }
        for (int offset : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), -588, 588})
            check(!make_read_offset_window(150, 151, offset).playable());
        check(!make_read_offset_window(0, 1, 1).playable());
        const auto positive = make_read_offset_window(0, 10, 1);
        check(positive.correctable_end_sample == 5879 && positive.playable_end_lba == 9);
        check(make_read_offset_window(150, 160, -1).playable_begin_lba == 151);
        for (bool wrong_start : {false, true}) {
            auto state = std::make_shared<RawState>();
            state->partial = !wrong_start; state->wrong_start = wrong_start;
            auto reader = make_offset_reader(std::make_unique<SampleReader>(state), make_read_offset_window(0, 300, 1));
            reader->seek(0);
            std::vector<std::int16_t> pcm(15 * cdda_samples_per_frame, 1234);
            const auto result = reader->read(pcm);
            check(result.status == ReadStatus::read_error && result.frames_read == 0);
            check(make_read_evidence(result).offset_status == OffsetStatus::uncorrected);
            check(result.offset->single_physical_read.has_value());
            check(std::all_of(pcm.begin(), pcm.end(), [](auto value) { return value == 1234; }));
            if (!wrong_start) check(result.retries == 1 && result.c2_status == C2Status::reported);
            bool rejected = false;
            try { (void)reader->read(pcm); } catch (const std::logic_error&) { rejected = true; }
            check(rejected);
        }
        for (int offset : {-1, 0, 1}) {
            auto state = std::make_shared<RawState>();
            auto window = make_read_offset_window(0, 300, offset);
            auto reader = make_repeated_read_verifier(
                make_offset_reader(std::make_unique<SampleReader>(state), window));
            auto p = window.playable_begin_lba;
            reader->seek(p);
            for (int block = 0; block < 2; ++block) {
                std::vector<std::int16_t> pcm(15 * cdda_samples_per_frame);
                const auto result = reader->read(pcm);
                check(result.status == ReadStatus::ok && result.verification.matching_reads == 2);
                check(result.offset && !result.offset->single_physical_read);
                const auto& attempt = result.verification.details[0];
                check(attempt.logical_start_lba == p - (block ? 15 : 0));
                check(attempt.logical_frames_requested == std::size_t(block ? 30 : 15));
                check(attempt.physical_frames_requested == attempt.logical_frames_requested + (offset % 588 != 0));
                check(attempt.frames_read == attempt.physical_frames_requested);
                for (std::size_t i = 0; i < pcm.size(); ++i)
                    check(pcm[i] == sample_at(std::int64_t(p) * 588 + offset + i / 2, i % 2));
                p += 15;
            }
        }
        // A corrected terminal window must drain accepted PCM, not fail and clear its queue.
        for (int offset : {-1, 1}) {
            auto state = std::make_shared<RawState>(); state->end = 32;
            const auto window = make_read_offset_window(0, 32, offset);
            PcmWorker worker([=] { return make_offset_reader(std::make_unique<SampleReader>(state), window); });
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0, 16}, 32));
            controller.set_playback_range(window.playable_begin_lba, window.playable_end_lba);
            struct Output final : AudioOutput {
                std::size_t total = 0;
                bool allow_drain = false;
                void reset() override {}
                std::size_t write(std::span<const std::int16_t> pcm) override { total += pcm.size()/2; return pcm.size()/2; }
                std::int64_t delay() override { return total; }
                bool drain() override { return allow_drain; }
            } output;
            PlaybackEngine engine(controller, worker, output, window.playable_end_lba);
            check(controller.play()); engine.synchronize();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!worker.status().done && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(worker.status().done && worker.status().error.empty());
            engine.tick();
            check(output.total == std::size_t(window.playable_end_lba - window.playable_begin_lba) * 588);
            check(controller.state().playback == PlaybackState::playing);
            const auto evidence = engine.read_diagnostics().current_playback;
            check(evidence && evidence->offset && evidence->offset->samples == offset);
            output.allow_drain = true; engine.tick();
            check(controller.state().playback == PlaybackState::stopped);
            check(!engine.read_diagnostics().current_playback);
        }
        std::cout << "PASS: offset PCM, sector boundaries, failures, repeat and terminal drain\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
