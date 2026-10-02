#include "media_state.hpp"
#include <utility>

MediaLifecycleState MediaStateTracker::observe(MediaObservation observation) noexcept {
    if (state_ == MediaLifecycleState::eject_error && observation == MediaObservation::audio_disc)
        return state_;
    switch (observation) {
    case MediaObservation::tray_open:
    case MediaObservation::no_disc:
        state_ = MediaLifecycleState::no_disc;
        observed_empty_ = true;
        error_.clear();
        break;
    case MediaObservation::not_ready:
        // Spin-up and transient drive recovery are not proof of removal.
        state_ = MediaLifecycleState::loading;
        error_.clear();
        break;
    case MediaObservation::audio_disc:
        state_ = MediaLifecycleState::audio_ready;
        error_.clear();
        break;
    case MediaObservation::unsupported_disc:
        state_ = MediaLifecycleState::unsupported;
        observed_empty_ = false;
        error_.clear();
        break;
    case MediaObservation::unknown:
        // Lack of information must not fabricate insertion or removal.
        break;
    }
    return state_;
}

bool MediaStateTracker::take_audio_insertion() noexcept {
    if (state_ != MediaLifecycleState::audio_ready || !observed_empty_) return false;
    observed_empty_ = false;
    return true;
}

void MediaStateTracker::begin_eject() noexcept {
    state_ = MediaLifecycleState::ejecting;
    error_.clear();
}

void MediaStateTracker::eject_failed(std::string error) {
    state_ = MediaLifecycleState::eject_error;
    error_ = std::move(error);
}
