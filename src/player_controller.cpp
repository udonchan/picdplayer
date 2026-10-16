#include "player_controller.hpp"
#include <algorithm>
#include <stdexcept>

void PlayerController::load_disc(const DiscToc& toc) {
    // DiscToc is publicly mutable: validate before replacing the current disc.
    if (toc.tracks.empty()) throw std::invalid_argument("empty disc");
    std::vector<std::int32_t> starts;
    for (const auto& track : toc.tracks) starts.push_back(track.start_lba);
    auto validated = make_audio_toc(toc.tracks.front().number, starts, toc.leadout_lba);
    for (std::size_t i = 0; i < toc.tracks.size(); ++i)
        if (toc.tracks[i].number != validated.tracks[i].number ||
            toc.tracks[i].length_frames != validated.tracks[i].length_frames)
            throw std::invalid_argument("inconsistent disc tracks");
    toc_ = std::move(validated);
    begin_ = toc_->tracks.front().start_lba;
    end_ = toc_->leadout_lba;
    state_.playback = PlaybackState::stopped;
    select_index(0);
}
void PlayerController::remove_disc() {
    toc_.reset();
    state_ = {};
    index_ = 0;
}
void PlayerController::set_playback_range(std::int32_t begin, std::int32_t end) {
    if (!toc_ || begin < toc_->tracks.front().start_lba || end > toc_->leadout_lba || end < begin)
        throw std::invalid_argument("invalid playback range");
    begin_ = begin;
    end_ = end;
}
bool PlayerController::select_index(std::size_t index, bool constrain) {
    auto position = toc_->tracks[index].start_lba;
    if (constrain) {
        const auto first = std::max(position, begin_);
        const auto last = std::min<std::int64_t>(toc_->tracks[index].start_lba + toc_->tracks[index].length_frames, end_);
        if (first >= last) return false;
        position = first;
    }
    index_ = index;
    state_.track = toc_->tracks[index].number;
    state_.position_lba = position;
    return true;
}
bool PlayerController::play() {
    if (!toc_) return false;
    const auto first = std::max(toc_->tracks[index_].start_lba, begin_);
    const auto last = std::min<std::int64_t>(toc_->tracks[index_].start_lba + toc_->tracks[index_].length_frames, end_);
    if (first >= last) return false;
    state_.position_lba = static_cast<std::int32_t>(std::clamp<std::int64_t>(*state_.position_lba, first, last - 1));
    state_.playback = PlaybackState::playing;
    return true;
}
void PlayerController::pause() {
    if (state_.playback == PlaybackState::playing) state_.playback = PlaybackState::paused;
}
void PlayerController::stop() {
    if (!toc_) return;
    state_.playback = PlaybackState::stopped;
    select_index(0, false);
}
bool PlayerController::select_track(int number) {
    if (!toc_) return false;
    const auto it = std::find_if(toc_->tracks.begin(), toc_->tracks.end(),
        [number](const Track& track) { return track.number == number; });
    if (it == toc_->tracks.end()) return false;
    return select_index(static_cast<std::size_t>(it - toc_->tracks.begin()));
}
bool PlayerController::next() {
    if (!toc_ || begin_ >= end_) return false;
    return index_ + 1 == toc_->tracks.size() || select_index(index_ + 1);
}
bool PlayerController::previous() {
    if (!toc_) return false;
    constexpr auto restart_threshold_frames = 3 * cd_frames_per_second;
    const auto elapsed = std::int64_t(*state_.position_lba) - toc_->tracks[index_].start_lba;
    return select_index(index_ == 0 || elapsed >= restart_threshold_frames ? index_ : index_ - 1);
}
bool PlayerController::seek_relative(std::int64_t frames) {
    if (!toc_ || begin_ >= end_) return false;
    const std::int64_t current = *state_.position_lba;
    const std::int64_t first = toc_->tracks.front().start_lba;
    const std::int64_t last = toc_->leadout_lba - 1;
    // Clamp the delta before addition, including INT64_MIN/MAX inputs.
    const auto target = std::clamp(current + std::clamp(frames, first - current, last - current),
                                   std::int64_t(begin_), std::int64_t(end_) - 1);
    auto it = std::upper_bound(toc_->tracks.begin(), toc_->tracks.end(), target,
        [](std::int64_t lba, const Track& track) { return lba < track.start_lba; });
    index_ = static_cast<std::size_t>(it - toc_->tracks.begin() - 1);
    state_.track = toc_->tracks[index_].number;
    state_.position_lba = static_cast<std::int32_t>(target);
    return true;
}

void PlayerController::playback_position(std::int32_t lba) {
    if (!toc_ || state_.playback != PlaybackState::playing) return;
    if (lba < *state_.position_lba || lba >= toc_->leadout_lba) return;
    seek_relative(std::int64_t(lba) - *state_.position_lba);
}
void PlayerController::finished() {
    if (!toc_) return;
    state_.playback = PlaybackState::stopped;
    select_index(0, false);
}
