#include "playback_engine.hpp"
#include <algorithm>
#include <stdexcept>

PlaybackEngine::PlaybackEngine(PlayerController& c, PcmWorker& w, AudioOutput& a, std::int32_t end)
    : controller_(c), worker_(w), output_(a), end_(end) {}
void PlaybackEngine::synchronize() {
    active_ = false;
    worker_.cancel();
    output_.reset();
    block_ = {}; offset_ = 0; submitted_ = 0; primed_ = false; draining_ = false;
    const auto state = controller_.state();
    if (state.playback == PlaybackState::playing) {
        start_ = *state.position_lba;
        generation_ = worker_.start(start_, end_);
        active_ = true;
    }
}
void PlaybackEngine::tick() {
    if (!active_) return;
    try {
        const auto status = worker_.status();
        if (!status.error.empty()) throw std::runtime_error(status.error);
        if (draining_) {
            if (output_.drain()) { controller_.finished(); synchronize(); }
            return;
        }
        if (!primed_) {
            if (status.queued < 5 && !status.done) return; // 1 sec prebuffer, shorter at disc end.
            primed_ = true;
        }
        // Bound work per main-loop iteration even for a sink that never blocks.
        for (int i = 0; i < 12; ++i) {
            if (offset_ == block_.samples.size()) {
                block_ = {}; offset_ = 0;
                if (!worker_.pop(block_)) break;
                if (block_.generation != generation_) { block_ = {}; continue; }
            }
            const auto remaining = std::span<const std::int16_t>(block_.samples).subspan(offset_);
            const auto accepted = output_.write(remaining);
            if (accepted > remaining.size() / 2) throw std::runtime_error("invalid audio write count");
            offset_ += accepted * 2;
            submitted_ += static_cast<std::int64_t>(accepted);
            if (accepted == 0) break;
        }
        const auto played = std::clamp<std::int64_t>(submitted_ - output_.delay(), 0, submitted_);
        controller_.playback_position(static_cast<std::int32_t>(std::min<std::int64_t>(end_ - 1, start_ + played / 588)));
        const auto after = worker_.status();
        if (after.done && after.queued == 0 && offset_ == block_.samples.size()) {
            draining_ = true;
            if (output_.drain()) { controller_.finished(); synchronize(); }
        }
    } catch (...) {
        controller_.stop();
        active_ = false;
        worker_.cancel();
        output_.reset();
        throw;
    }
}
