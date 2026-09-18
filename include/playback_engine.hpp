#pragma once
#include "audio_output.hpp"
#include "pcm_worker.hpp"
#include "player_controller.hpp"
#include <chrono>
#include <deque>

class PlaybackEngine {
public:
    PlaybackEngine(PlayerController& controller, PcmWorker& worker, AudioOutput& output, std::int32_t end);
    void set_disc_end(std::int32_t end);
    // Call only while stopped, before starting a stream with a new reader plan.
    void reset_prebuffer_target();
    // Call once after a position/state command; invalidates all old PCM.
    void synchronize();
    void tick();
    ReadDiagnostics read_diagnostics();
private:
    PlayerController& controller_;
    PcmWorker& worker_;
    AudioOutput& output_;
    std::int32_t end_, start_ = 0;
    std::int64_t submitted_ = 0;
    std::uint64_t generation_ = 0;
    PcmBlock block_;
    std::size_t offset_ = 0;
    bool active_ = false, primed_ = false, draining_ = false;
    std::size_t prebuffer_blocks_ = 0;
    std::chrono::steady_clock::time_point prebuffer_started_at_{};
    std::optional<std::int64_t> last_prebuffer_wait_ms_;
    unsigned underrun_recoveries_ = 0;
    std::chrono::steady_clock::time_point last_tick_{};
    struct SubmittedEvidence {
        std::int64_t begin_stereo_frame;
        std::int64_t end_stereo_frame;
        ReadEvidence evidence;
    };
    std::deque<SubmittedEvidence> submitted_evidence_;
    std::optional<ReadEvidence> current_evidence_;
};
