#pragma once

#include "disc_toc.hpp"
#include "media_state.hpp"
#include "metadata_model.hpp"
#include "player_controller.hpp"
#include <cstdint>
#include <optional>

// Read-only projection for future API/UI consumers. PlayerController,
// MediaStateTracker and MetadataSession remain the authoritative owners.
struct DaemonSnapshot {
    std::uint64_t revision = 0;
    PlayerState player;
    MediaLifecycleState media = MediaLifecycleState::no_disc;
    std::optional<DiscToc> disc;
    MetadataResult metadata;
    std::optional<std::int64_t> position_in_track_frames;
    std::optional<std::int64_t> current_track_length_frames;
};

// Builds a self-contained value without hardware or network access. Throws
// invalid_argument if the independently supplied owner snapshots disagree.
DaemonSnapshot make_daemon_snapshot(std::uint64_t revision,
                                    const PlayerState& player,
                                    MediaLifecycleState media,
                                    const std::optional<DiscToc>& disc,
                                    const MetadataResult& metadata);
