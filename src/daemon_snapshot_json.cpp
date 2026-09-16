#include "daemon_snapshot_json.hpp"
#include <nlohmann/json.hpp>

using Json = nlohmann::json;
namespace {
const char* playback_name(PlaybackState state) {
    switch (state) {
    case PlaybackState::no_disc: return "NO_DISC";
    case PlaybackState::stopped: return "STOPPED";
    case PlaybackState::playing: return "PLAYING";
    case PlaybackState::paused: return "PAUSED";
    }
    return "NO_DISC";
}
const char* media_name(MediaLifecycleState state) {
    switch (state) {
    case MediaLifecycleState::no_disc: return "NO_DISC";
    case MediaLifecycleState::loading: return "LOADING";
    case MediaLifecycleState::audio_ready: return "AUDIO_READY";
    case MediaLifecycleState::unsupported: return "UNSUPPORTED";
    }
    return "NO_DISC";
}
const char* metadata_name(MetadataStatus state) {
    switch (state) {
    case MetadataStatus::not_requested: return "NOT_REQUESTED";
    case MetadataStatus::loading: return "LOADING";
    case MetadataStatus::available: return "AVAILABLE";
    case MetadataStatus::not_found: return "NOT_FOUND";
    case MetadataStatus::ambiguous: return "AMBIGUOUS";
    case MetadataStatus::error: return "ERROR";
    }
    return "ERROR";
}
const char* artwork_name(ArtworkStatus state) {
    switch (state) {
    case ArtworkStatus::not_requested: return "NOT_REQUESTED";
    case ArtworkStatus::available: return "AVAILABLE";
    case ArtworkStatus::unavailable: return "UNAVAILABLE";
    case ArtworkStatus::error: return "ERROR";
    }
    return "ERROR";
}
template<class T> Json optional(const std::optional<T>& value) {
    return value ? Json(*value) : Json(nullptr);
}
Json track_metadata(const TrackMetadata& track) {
    return {{"track_number", track.track_number}, {"title", track.title}, {"artist", track.artist},
            {"recording_id", track.recording_id}, {"source_length_ms", optional(track.source_length_ms)}};
}
Json disc_metadata(const DiscMetadata& disc) {
    Json tracks = Json::array();
    for (const auto& track : disc.tracks) tracks.push_back(track_metadata(track));
    return {{"release_id", disc.release_id}, {"release_group_id", disc.release_group_id},
            {"album_title", disc.album_title}, {"album_artist", disc.album_artist},
            {"country", disc.country}, {"date", disc.date},
            {"medium_position", disc.medium_position}, {"medium_title", disc.medium_title},
            {"tracks", std::move(tracks)}};
}
}

std::string serialize_daemon_snapshot(const DaemonSnapshot& snapshot) {
    Json root;
    root["revision"] = snapshot.revision;
    root["player"] = {{"state", playback_name(snapshot.player.playback)},
                      {"track", optional(snapshot.player.track)},
                      {"position_lba", optional(snapshot.player.position_lba)},
                      {"position_in_track_frames", optional(snapshot.position_in_track_frames)},
                      {"current_track_length_frames", optional(snapshot.current_track_length_frames)}};
    root["media"] = {{"state", media_name(snapshot.media)}};
    if (snapshot.disc) {
        Json tracks = Json::array();
        for (const auto& track : snapshot.disc->tracks)
            tracks.push_back({{"number", track.number}, {"start_lba", track.start_lba},
                              {"length_frames", track.length_frames}});
        root["disc"] = {{"track_count", snapshot.disc->tracks.size()},
                        {"leadout_lba", snapshot.disc->leadout_lba}, {"tracks", std::move(tracks)}};
    } else root["disc"] = nullptr;

    Json candidates = Json::array();
    for (const auto& candidate : snapshot.metadata.candidates)
        candidates.push_back(disc_metadata(candidate.metadata));
    root["metadata"] = {{"status", metadata_name(snapshot.metadata.status)},
                        {"disc_id", snapshot.metadata.disc_id}, {"toc", snapshot.metadata.toc},
                        {"from_cache", snapshot.metadata.from_cache},
                        {"error", snapshot.metadata.error},
                        {"selected", optional(snapshot.metadata.selected)},
                        {"candidates", std::move(candidates)},
                        {"cover_art", {{"status", artwork_name(snapshot.metadata.artwork.status)},
                                       {"image_url", snapshot.metadata.artwork.image_url},
                                       {"error", snapshot.metadata.artwork.error}}}};
    return root.dump();
}
