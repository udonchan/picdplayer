#pragma once
#include <string>

enum class MediaObservation {
    tray_open,
    no_disc,
    not_ready,
    audio_disc,
    unsupported_disc,
    unknown,
};

enum class MediaLifecycleState { no_disc, loading, audio_ready, unsupported, ejecting, eject_error };

// Converts repeated one-shot drive observations into an application-level
// lifecycle. It owns no hardware and does not modify PlayerController.
class MediaStateTracker {
public:
    MediaLifecycleState state() const noexcept { return state_; }
    const std::string& error() const noexcept { return error_; }
    MediaLifecycleState observe(MediaObservation observation) noexcept;
    void begin_eject() noexcept;
    void eject_failed(std::string error);

private:
    MediaLifecycleState state_ = MediaLifecycleState::no_disc;
    std::string error_;
};
