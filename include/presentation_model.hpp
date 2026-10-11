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

struct PresentationDiscLayout {
    DiscToc toc;
    std::string session_id;
    std::uint64_t disc_generation = 0;
};

struct PresentationDisc {
    MediaLifecycleState state = MediaLifecycleState::no_disc;
    std::optional<PresentationDiscLayout> layout;
    std::optional<std::string> title;
    std::optional<std::string> artist;
};

struct PresentationEnrichment {
    MetadataStatus status = MetadataStatus::not_requested;
    struct Candidate {
        std::size_t index = 0;
        std::optional<std::string> title;
        std::optional<std::string> artist;
        std::optional<std::string> country;
        std::optional<std::string> date;
        std::optional<int> medium_position;
        std::optional<std::string> medium_title;
        std::size_t track_count = 0;
    };
    struct Selection {
        std::string session_id;
        std::uint64_t disc_generation = 0;
        std::uint64_t metadata_generation = 0;
        std::optional<std::size_t> selected_index;
        bool candidates_declined = false;
        std::vector<Candidate> candidates;
    };
    std::optional<Selection> selection;
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
                                           bool has_cover_asset = false,
                                           std::optional<std::uint64_t> disc_generation = std::nullopt,
                                           std::optional<std::uint64_t> metadata_generation = std::nullopt);
