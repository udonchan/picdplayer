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
    bool play();
    void pause();
    void stop();
    bool select_track(int number);
    bool next();
    bool previous();
    void playback_position(std::int32_t lba);
    void finished();
    bool seek_relative(std::int64_t frames);
    void set_playback_range(std::int32_t begin, std::int32_t end);
private:
    std::optional<DiscToc> toc_;
    PlayerState state_;
    std::size_t index_ = 0;
    std::int32_t begin_ = 0, end_ = 0;
    bool select_index(std::size_t index, bool constrain = true);
};
