#include "media_state.hpp"

MediaLifecycleState MediaStateTracker::observe(MediaObservation observation) noexcept {
    switch (observation) {
    case MediaObservation::tray_open:
    case MediaObservation::no_disc:
        state_ = MediaLifecycleState::no_disc;
        break;
    case MediaObservation::not_ready:
        // Spin-up and transient drive recovery are not proof of removal.
        state_ = MediaLifecycleState::loading;
        break;
    case MediaObservation::audio_disc:
        state_ = MediaLifecycleState::audio_ready;
        break;
    case MediaObservation::unsupported_disc:
        state_ = MediaLifecycleState::unsupported;
        break;
    case MediaObservation::unknown:
        // Lack of information must not fabricate insertion or removal.
        break;
    }
    return state_;
}
