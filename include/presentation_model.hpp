#pragma once

#include "daemon_snapshot.hpp"
#include <optional>
#include <string>
#include <vector>

struct PresentationTrack {
    int number = 0;
    std::int64_t duration_frames = 0;
    std::optional<std::string> title;
    std::optional<std::string> artist;
};

struct PresentationDisc {
    MediaLifecycleState state = MediaLifecycleState::no_disc;
    std::optional<std::string> title;
    std::optional<std::string> artist;
};

struct PresentationEnrichment {
    MetadataStatus status = MetadataStatus::not_requested;
};

struct PresentationArtwork {
    ArtworkStatus status = ArtworkStatus::not_requested;
    // Empty means that no same-origin local artwork resource is available.
    std::optional<std::string> cover_url;
};

// This is the UI contract. Provider IDs, URLs and cache implementation detail
// deliberately do not appear here.
struct PresentationModel {
    std::uint64_t revision = 0;
    PlayerState player;
    std::optional<std::int64_t> position_frames;
    std::optional<std::int64_t> track_duration_frames;
    PresentationDisc disc;
    std::vector<PresentationTrack> tracks;
    PresentationEnrichment enrichment;
    PresentationArtwork artwork;
    DriveCapabilities drive;
    ReadDiagnostics read;
    std::vector<PlayerEvent> recent_events;
};

PresentationModel make_presentation_model(std::uint64_t revision, const PlayerState& player,
                                           MediaLifecycleState media, const std::optional<DiscToc>& disc,
                                           const MetadataResult& enrichment, DriveCapabilities drive,
                                           ReadDiagnostics read, std::vector<PlayerEvent> recent_events,
                                           bool has_cover_asset = false);
