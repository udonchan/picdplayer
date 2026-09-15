#pragma once
#include "audio_output.hpp"
#include "pcm_worker.hpp"
#include "player_controller.hpp"

class PlaybackEngine {
public:
    PlaybackEngine(PlayerController& controller, PcmWorker& worker, AudioOutput& output, std::int32_t end);
    // Call once after a position/state command; invalidates all old PCM.
    void synchronize();
    void tick();
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
};
