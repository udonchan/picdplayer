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
    // True once after an accepted audio disc follows an observed empty tray.
    // The initial no_disc state alone is not insertion evidence.
    bool take_audio_insertion() noexcept;

private:
    MediaLifecycleState state_ = MediaLifecycleState::no_disc;
    std::string error_;
    bool observed_empty_ = false;
};
