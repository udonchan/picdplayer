#include "media_state.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value) {
    if (!value) throw std::runtime_error("media state test failed");
}

int main() {
    try {
        MediaStateTracker media;
        check(media.state() == MediaLifecycleState::no_disc);
        check(media.observe(MediaObservation::unknown) == MediaLifecycleState::no_disc);
        check(media.observe(MediaObservation::not_ready) == MediaLifecycleState::loading);
        check(media.observe(MediaObservation::unknown) == MediaLifecycleState::loading);
        check(media.observe(MediaObservation::audio_disc) == MediaLifecycleState::audio_ready);
        check(media.observe(MediaObservation::not_ready) == MediaLifecycleState::loading);
        check(media.observe(MediaObservation::audio_disc) == MediaLifecycleState::audio_ready);
        check(media.observe(MediaObservation::no_disc) == MediaLifecycleState::no_disc);
        check(media.observe(MediaObservation::unsupported_disc) == MediaLifecycleState::unsupported);
        check(media.observe(MediaObservation::tray_open) == MediaLifecycleState::no_disc);
        check(media.observe(MediaObservation::tray_open) == MediaLifecycleState::no_disc);
        std::cout << "PASS: media lifecycle observations\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
