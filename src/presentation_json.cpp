#include "presentation_json.hpp"
#include "diagnostics_json.hpp"
#include "daemon_snapshot_json.hpp"
#include <nlohmann/json.hpp>

using Json = nlohmann::json;
namespace {
const char* playback_name(PlaybackState state) { return state == PlaybackState::playing ? "PLAYING" : state == PlaybackState::paused ? "PAUSED" : state == PlaybackState::stopped ? "STOPPED" : "NO_DISC"; }
const char* media_name(MediaLifecycleState state) { return state == MediaLifecycleState::audio_ready ? "AUDIO_READY" : state == MediaLifecycleState::loading ? "LOADING" : state == MediaLifecycleState::unsupported ? "UNSUPPORTED" : state == MediaLifecycleState::ejecting ? "EJECTING" : state == MediaLifecycleState::eject_error ? "EJECT_ERROR" : "NO_DISC"; }
const char* metadata_name(MetadataStatus state) { return state == MetadataStatus::loading ? "LOADING" : state == MetadataStatus::available ? "AVAILABLE" : state == MetadataStatus::not_found ? "NOT_FOUND" : state == MetadataStatus::ambiguous ? "UNAVAILABLE" : state == MetadataStatus::error ? "ERROR" : "NOT_REQUESTED"; }
template<class T> Json optional(const std::optional<T>& v) { return v ? Json(*v) : Json(nullptr); }
}

std::string serialize_presentation_model(const PresentationModel& model) {
    Json tracks = Json::array();
    for (const auto& track : model.tracks)
        tracks.push_back({{"number", track.number}, {"duration_frames", track.duration_frames},
                          {"title", optional(track.title)}, {"artist", optional(track.artist)}});
    const auto& player = model.player;
    Json root{{"schema_version", 1}, {"revision", model.revision},
              {"player", {{"state", playback_name(player.playback)}, {"track_number", optional(player.track)},
                          {"position_frames", optional(model.position_frames)},
                          {"track_duration_frames", optional(model.track_duration_frames)}}},
              {"disc", {{"state", media_name(model.disc.state)}, {"title", optional(model.disc.title)},
                        {"artist", optional(model.disc.artist)}}}, {"tracks", std::move(tracks)},
              {"enrichment", {{"status", metadata_name(model.enrichment.status)}}},
              {"artwork", {{"cover", model.artwork.cover_url ? Json{{"url", *model.artwork.cover_url}, {"mime_type", nullptr}, {"width", nullptr}, {"height", nullptr}} : Json(nullptr)}}}};
    root.update(diagnostic_fields(model.drive, model.read, model.recent_events));
    return root.dump();
}

bool presentation_json_equal_ignoring_revision(std::string_view left, std::string_view right) {
    return snapshot_json_equal_ignoring_revision(left, right);
}
