#pragma once

enum class MediaObservation {
    tray_open,
    no_disc,
    not_ready,
    audio_disc,
    unsupported_disc,
    unknown,
};

enum class MediaLifecycleState { no_disc, loading, audio_ready, unsupported };

// Converts repeated one-shot drive observations into an application-level
// lifecycle. It owns no hardware and does not modify PlayerController.
class MediaStateTracker {
public:
    MediaLifecycleState state() const noexcept { return state_; }
    MediaLifecycleState observe(MediaObservation observation) noexcept;

private:
    MediaLifecycleState state_ = MediaLifecycleState::no_disc;
};
