#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class MetadataStatus { not_requested, loading, available, not_found, ambiguous, error };
enum class ArtworkStatus { not_requested, available, unavailable, error };

struct TrackMetadata {
    int track_number = 0;
    std::string title;
    std::string artist;
    std::string recording_id;
    std::optional<std::int64_t> source_length_ms;
};

struct DiscMetadata {
    std::string release_id;
    std::string release_group_id;
    std::string album_title;
    std::string album_artist;
    std::string country;
    std::string date;
    int medium_position = 0;
    std::string medium_title;
    std::vector<TrackMetadata> tracks;
};

struct ReleaseCandidate { DiscMetadata metadata; };

struct ArtworkInfo {
    ArtworkStatus status = ArtworkStatus::not_requested;
    // Provider URL is enrichment-internal. It must not be published to UI JSON.
    std::string image_url;
    std::string mime_type;
    std::string error;
};

struct MetadataResult {
    MetadataStatus status = MetadataStatus::not_requested;
    std::string disc_id;
    std::string toc;
    std::vector<ReleaseCandidate> candidates;
    std::optional<std::size_t> selected;
    ArtworkInfo artwork;
    std::string error;
    bool from_cache = false;
};

const char* metadata_status_name(MetadataStatus status);
