#include "playback_engine.hpp"
#include "logger.hpp"
#include <algorithm>
#include <stdexcept>

PlaybackEngine::PlaybackEngine(PlayerController& c, PcmWorker& w, AudioOutput& a, std::int32_t end)
    : controller_(c), worker_(w), output_(a), end_(end),
      prebuffer_blocks_(w.startup_buffer_blocks()) {
    if (end < 0) throw std::invalid_argument("invalid disc end");
}
void PlaybackEngine::set_disc_end(std::int32_t end) {
    if (end < 0) throw std::invalid_argument("invalid disc end");
    end_ = end;
}
void PlaybackEngine::reset_prebuffer_target() {
    if (active_) throw std::logic_error("cannot change prebuffer target while active");
    prebuffer_blocks_ = worker_.startup_buffer_blocks();
}
void PlaybackEngine::synchronize() {
    last_tick_ = std::chrono::steady_clock::now();
    active_ = false;
    worker_.cancel();
    output_.reset();
    block_ = {}; offset_ = 0; submitted_ = 0; primed_ = false; draining_ = false;
    prebuffer_started_at_ = std::chrono::steady_clock::now();
    last_prebuffer_wait_ms_.reset();
    submitted_evidence_.clear(); current_evidence_.reset();
    const auto state = controller_.state();
    if (state.playback == PlaybackState::playing) {
        if (end_ <= *state.position_lba) throw std::runtime_error("disc end is unavailable");
        start_ = *state.position_lba;
        generation_ = worker_.start(start_, end_);
        active_ = true;
    }
}
void PlaybackEngine::tick() {
    if (!active_) return;
    const auto now = std::chrono::steady_clock::now();
    const auto gap_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick_).count();
    last_tick_ = now;
    try {
        const auto status = worker_.status();
        if (!status.error.empty()) throw std::runtime_error(status.error);
        if (draining_) {
            if (output_.drain()) { controller_.finished(); synchronize(); }
            return;
        }
        if (!primed_) {
            if (status.queued < prebuffer_blocks_ && !status.done)
                return; // Adaptive prebuffer, shorter at disc end.
            primed_ = true;
            last_prebuffer_wait_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - prebuffer_started_at_).count();
            log_info("player") << "prebuffer_ready wait_ms=" << *last_prebuffer_wait_ms_
                               << " queued_blocks=" << status.queued
                               << " target_frames="
                               << prebuffer_blocks_ * worker_.read_block_cd_frames();
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
            if (accepted) {
                submitted_evidence_.push_back({submitted_, submitted_ + static_cast<std::int64_t>(accepted),
                                               block_.evidence});
            }
            offset_ += accepted * 2;
            submitted_ += static_cast<std::int64_t>(accepted);
            if (accepted == 0) break;
        }
        const auto played = std::clamp<std::int64_t>(submitted_ - output_.delay(), 0, submitted_);
        while (!submitted_evidence_.empty() && submitted_evidence_.front().end_stereo_frame <= played)
            submitted_evidence_.pop_front();
        if (!submitted_evidence_.empty() && submitted_evidence_.front().begin_stereo_frame <= played)
            current_evidence_ = submitted_evidence_.front().evidence;
        else
            current_evidence_.reset();
        controller_.playback_position(static_cast<std::int32_t>(std::min<std::int64_t>(end_ - 1, start_ + played / 588)));
        const auto after = worker_.status();
        if (after.done && after.queued == 0 && offset_ == block_.samples.size()) {
            draining_ = true;
            if (output_.drain()) { controller_.finished(); synchronize(); }
        }
    } catch (const AudioUnderrun& error) {
        const auto status = worker_.status();
        log_warning("player") << "failure_context tick_gap_us=" << gap_us
                              << " queued_blocks=" << status.queued
                              << " pending_samples=" << (block_.samples.size() - offset_)
                              << " submitted_stereo_frames=" << submitted_
                              << " last_read_us=" << status.last_read_us
                              << " read_inflight_us=" << status.read_inflight_us;
        try {
            // At XRUN ALSA has consumed everything it accepted. Resume at the
            // last whole CD frame submitted, avoiding a large audible repeat.
            const auto resume = static_cast<std::int32_t>(std::min<std::int64_t>(
                end_ - 1, start_ + submitted_ / 588));
            controller_.playback_position(resume);
            active_ = false;
            worker_.discard_reader();
            output_.reset();
            block_ = {}; offset_ = 0; submitted_ = 0; primed_ = false; draining_ = false;
            prebuffer_started_at_ = std::chrono::steady_clock::now();
            last_prebuffer_wait_ms_.reset();
            submitted_evidence_.clear(); current_evidence_.reset();
            start_ = *controller_.state().position_lba;
            prebuffer_blocks_ = std::min(worker_.buffer_capacity_blocks(),
                                         prebuffer_blocks_ + std::size_t{5});
            ++underrun_recoveries_;
            generation_ = worker_.start(start_, end_);
            active_ = true;
            last_tick_ = std::chrono::steady_clock::now();
            log_warning("player") << "underrun recovery=" << underrun_recoveries_
                                  << " resume_lba=" << start_
                                  << " prebuffer_blocks=" << prebuffer_blocks_
                                  << " reason=" << error.what();
            return;
        } catch (...) {
            controller_.stop();
            active_ = false;
            worker_.cancel();
            worker_.discard_reader();
            try { output_.reset(); } catch (...) {}
            throw;
        }
    } catch (...) {
        const auto status = worker_.status();
        log_warning("player") << "failure_context tick_gap_us=" << gap_us
                              << " queued_blocks=" << status.queued
                              << " pending_samples=" << (block_.samples.size() - offset_)
                              << " submitted_stereo_frames=" << submitted_
                              << " last_read_us=" << status.last_read_us
                              << " read_inflight_us=" << status.read_inflight_us;
        controller_.stop();
        active_ = false;
        worker_.cancel();
        // A USB reset or media change can leave the open drive handle unusable.
        // Recreate it on the worker thread before a later Play command.
        worker_.discard_reader();
        output_.reset();
        throw;
    }
}

ReadDiagnostics PlaybackEngine::read_diagnostics() {
    const auto status = worker_.status(true);
    auto result = status.diagnostics;
    result.current_playback = current_evidence_;
    result.queued_blocks = status.queued;
    result.prebuffer_target_frames = prebuffer_blocks_ * worker_.read_block_cd_frames();
    result.last_prebuffer_wait_ms = last_prebuffer_wait_ms_;
    return result;
}
