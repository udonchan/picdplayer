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
    case MediaLifecycleState::ejecting: return "EJECTING";
    case MediaLifecycleState::eject_error: return "EJECT_ERROR";
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
    root["schema_version"] = 1;
    root["revision"] = snapshot.revision;
    root["player"] = {{"state", playback_name(snapshot.player.playback)},
                      {"track", optional(snapshot.player.track)},
                      {"position_lba", optional(snapshot.player.position_lba)},
                      {"position_in_track_frames", optional(snapshot.position_in_track_frames)},
                      {"current_track_length_frames", optional(snapshot.current_track_length_frames)}};
    root["media"] = {{"state", media_name(snapshot.media)}, {"error", snapshot.media_error}};
    const auto capability = [](const CapabilityFlag& value) {
        return Json{{"value", knowledge_name(value.value)},
                    {"source", capability_evidence_source_name(value.source)},
                    {"detail", value.detail}};
    };
    root["drive"] = {{"device", snapshot.drive.device},
                     {"vendor", snapshot.drive.vendor},
                     {"model", snapshot.drive.model},
                     {"firmware", snapshot.drive.firmware},
                     {"probe_error", snapshot.drive.probe_error},
                     {"digital_audio_extraction", capability(snapshot.drive.digital_audio_extraction)},
                     {"c2_supported", capability(snapshot.drive.c2_supported)},
                     {"c2_trustworthy", capability(snapshot.drive.c2_trustworthy)},
                     {"read_cache", capability(snapshot.drive.read_cache)},
                     {"accurate_stream", capability(snapshot.drive.accurate_stream)},
                     {"speed_control", capability(snapshot.drive.speed_control)},
                     {"current_speed_x", optional(snapshot.drive.current_speed_x)},
                     {"read_offset_samples", optional(snapshot.drive.read_offset_samples)}};
    const auto read_evidence = [](const std::optional<ReadEvidence>& source) -> Json {
        if (!source) return nullptr;
        const auto& evidence = *source;
        return {{"start_lba", evidence.start_lba},
                  {"frames_requested", evidence.frames_requested},
                  {"frames_read", evidence.frames_read},
                  {"status", integrity_read_status_name(evidence.status)},
                  {"local_verification", local_verification_name(evidence.local_verification)},
                  {"c2_status", c2_status_name(evidence.c2_status)},
                  {"offset_status", offset_status_name(evidence.offset_status)},
                  {"direct_retries", evidence.direct_retries},
                  {"verification", {{"attempts", evidence.verification.attempts},
                                    {"complete_reads", evidence.verification.complete_reads},
                                    {"matching_reads", evidence.verification.matching_reads},
                                    {"mismatches", evidence.verification.mismatches},
                                    {"time_budget_exhausted",
                                     evidence.verification.time_budget_exhausted}}},
                  {"backend_events", {{"reads", evidence.backend_events.reads},
                                      {"verifies", evidence.backend_events.verifies},
                                      {"fixups", evidence.backend_events.fixups},
                                      {"skips", evidence.backend_events.skips},
                                      {"read_errors", evidence.backend_events.read_errors},
                                      {"cache_errors", evidence.backend_events.cache_errors},
                                      {"other", evidence.backend_events.other}}}};
    };
    const auto& stats = snapshot.read.stats;
    root["read"] = {{"activity", read_activity_name(snapshot.read.activity)},
                    {"requested_mode", snapshot.read.requested_mode},
                    {"effective_strategy", snapshot.read.effective_strategy},
                    {"queued_blocks", snapshot.read.queued_blocks},
                    {"buffer_capacity_frames", snapshot.read.buffer_capacity_frames},
                    {"startup_buffer_frames", snapshot.read.startup_buffer_frames},
                    {"read_block_frames", snapshot.read.read_block_frames},
                    {"prebuffer_target_frames", snapshot.read.prebuffer_target_frames},
                    {"last_prebuffer_wait_ms", optional(snapshot.read.last_prebuffer_wait_ms)},
                    {"dropped_events", snapshot.read.dropped_events},
                    {"latest", read_evidence(snapshot.read.latest)},
                    {"current_playback", read_evidence(snapshot.read.current_playback)},
                    {"stats", {{"read_calls", stats.read_calls},
                               {"frames_requested", stats.frames_requested},
                               {"frames_accepted", stats.frames_accepted},
                               {"direct_retries", stats.direct_retries},
                               {"backend_reads", stats.backend_reads},
                               {"backend_verifies", stats.backend_verifies},
                               {"backend_fixups", stats.backend_fixups},
                               {"backend_skips", stats.backend_skips},
                               {"backend_read_errors", stats.backend_read_errors},
                               {"backend_cache_errors", stats.backend_cache_errors},
                               {"backend_other", stats.backend_other},
                               {"verification_attempts", stats.verification_attempts},
                               {"verification_mismatches", stats.verification_mismatches},
                               {"verified_calls", stats.verified_calls},
                               {"verification_failures", stats.verification_failures},
                               {"failed_calls", stats.failed_calls}}}};
    Json events = Json::array();
    for (const auto& event : snapshot.recent_events) {
        events.push_back({{"sequence", event.sequence},
                          {"stream_generation", event.stream_generation},
                          {"type", player_event_type_name(event.type)},
                          {"severity", event_severity_name(event.severity)},
                          {"presentation_priority", presentation_priority_name(event.presentation)},
                          {"region", {{"start_lba", event.read.start_lba},
                                      {"end_lba", event.read.start_lba +
                                                  static_cast<std::int32_t>(event.read.frames_read)}}},
                          {"read_status", integrity_read_status_name(event.read.status)}});
    }
    root["recent_events"] = std::move(events);
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
