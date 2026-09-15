#pragma once
#include "disc_toc.hpp"
#include <optional>

enum class PlaybackState { no_disc, stopped, playing, paused };
struct PlayerState {
    PlaybackState playback = PlaybackState::no_disc;
    std::optional<int> track;
    std::optional<std::int32_t> position_lba;
};

// Single-thread owner. Commands only update intent; no hardware access.
class PlayerController {
public:
    PlayerState state() const { return state_; } // snapshot, never mutable ownership
    void load_disc(const DiscToc& toc);
    void remove_disc();
    void play();
    void pause();
    void stop();
    bool select_track(int number);
    void next();
    void previous();
    void playback_position(std::int32_t lba);
    void finished();
    void seek_relative(std::int64_t frames);
private:
    std::optional<DiscToc> toc_;
    PlayerState state_;
    std::size_t index_ = 0;
    void select_index(std::size_t index);
};
