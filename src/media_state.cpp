#include "media_state.hpp"
#include <utility>

MediaLifecycleState MediaStateTracker::observe(MediaObservation observation) noexcept {
    if (state_ == MediaLifecycleState::eject_error && observation == MediaObservation::audio_disc)
        return state_;
    switch (observation) {
    case MediaObservation::tray_open:
    case MediaObservation::no_disc:
        state_ = MediaLifecycleState::no_disc;
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
        error_.clear();
        break;
    case MediaObservation::unknown:
        // Lack of information must not fabricate insertion or removal.
        break;
    }
    return state_;
}

void MediaStateTracker::begin_eject() noexcept {
    state_ = MediaLifecycleState::ejecting;
    error_.clear();
}

void MediaStateTracker::eject_failed(std::string error) {
    state_ = MediaLifecycleState::eject_error;
    error_ = std::move(error);
}
