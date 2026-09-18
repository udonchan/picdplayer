#include "daemon_snapshot_json.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace { void check(bool value) { if (!value) throw std::runtime_error("snapshot JSON test failed"); } }
int main() {
    try {
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 750);
        MetadataResult metadata; metadata.status = MetadataStatus::available;
        metadata.disc_id = "disc"; metadata.from_cache = true;
        DiscMetadata album; album.release_id = "release"; album.album_title = "Album";
        album.tracks.push_back({1, "Song", "Artist", "recording", 10000});
        metadata.candidates.push_back({album}); metadata.selected = 0;
        metadata.artwork.status = ArtworkStatus::available;
        metadata.artwork.image_url = "https://example.invalid/cover.jpg";
        ReadDiagnostics read;
        read.activity = ReadActivity::buffering;
        read.effective_strategy = "direct-single-read";
        read.latest = make_read_evidence({60, 15, 15, ReadStatus::ok, 0, 0});
        read.current_playback = make_read_evidence({45, 15, 15, ReadStatus::ok, 0, 0});
        read.queued_blocks = 8;
        read.buffer_capacity_frames = 300;
        read.startup_buffer_frames = 150;
        read.read_block_frames = 15;
        read.prebuffer_target_frames = 150;
        read.last_prebuffer_wait_ms = 1234;
        read.requested_policy = {ReadVerificationMode::repeat, 75, 2, 3, 10000};
        read.effective_policy = read.requested_policy;
        observe_read(read.stats, {60, 15, 15, ReadStatus::ok, 0, 0});
        DriveCapabilities drive;
        drive.device = "/dev/sr0"; drive.vendor = "ASUS";
        drive.speed_control = {Knowledge::yes, CapabilityEvidenceSource::kernel_reported,
                               "CDROM_GET_CAPABILITY CDC_SELECT_SPEED"};
        PlayerEvent event;
        event.sequence = 7; event.stream_generation = 3;
        event.read = *read.latest;
        const auto snapshot = make_daemon_snapshot(42,
            {PlaybackState::playing, 1, 75}, MediaLifecycleState::audio_ready, toc, metadata,
            {}, read, {event}, drive);
        const auto serialized = serialize_daemon_snapshot(snapshot);
        const auto json = nlohmann::json::parse(serialized);
        check(json["revision"] == 42 && json["player"]["state"] == "PLAYING");
        check(json["schema_version"] == 1);
        check(json["player"]["position_in_track_frames"] == 75);
        check(json["disc"]["tracks"][0]["length_frames"] == 750);
        check(json["metadata"]["status"] == "AVAILABLE" && json["metadata"]["selected"] == 0);
        check(json["metadata"]["candidates"][0]["tracks"][0]["title"] == "Song");
        check(json["metadata"]["cover_art"]["status"] == "AVAILABLE");
        check(json["read"]["activity"] == "BUFFERING");
        check(json["read"]["effective_strategy"] == "direct-single-read");
        check(json["read"]["latest"]["status"] == "CLEAN");
        check(json["read"]["latest"]["local_verification"] == "SINGLE_READ");
        check(json["read"]["latest"]["c2_status"] == "NOT_CHECKED");
        check(json["read"]["latest"]["offset_status"] == "UNKNOWN");
        check(json["read"]["latest"]["verification"]["attempts"] == 0);
        check(json["read"]["stats"]["read_calls"] == 1);
        check(json["read"]["stats"]["verified_calls"] == 0);
        check(json["read"]["current_playback"]["start_lba"] == 45);
        check(json["read"]["queued_blocks"] == 8);
        check(json["read"]["buffer_capacity_frames"] == 300);
        check(json["read"]["startup_buffer_frames"] == 150);
        check(json["read"]["read_block_frames"] == 15);
        check(json["read"]["prebuffer_target_frames"] == 150);
        check(json["read"]["last_prebuffer_wait_ms"] == 1234);
        check(json["read"]["policy"]["effective"]["mode"] == "REPEAT");
        check(json["drive"]["vendor"] == "ASUS");
        check(json["drive"]["digital_audio_extraction"]["value"] == "UNKNOWN");
        check(json["drive"]["speed_control"]["value"] == "YES");
        check(json["recent_events"][0]["sequence"] == 7);
        check(json["recent_events"][0]["type"] == "READ_OBSERVED");
        const auto next_revision = serialize_daemon_snapshot(make_daemon_snapshot(43,
            {PlaybackState::playing, 1, 75}, MediaLifecycleState::audio_ready, toc, metadata,
            {}, read, {event}, drive));
        check(snapshot_json_equal_ignoring_revision(serialized, next_revision));
        const auto eject_error = nlohmann::json::parse(serialize_daemon_snapshot(
            make_daemon_snapshot(43, {PlaybackState::playing, 1, 75},
                                 MediaLifecycleState::eject_error, toc, metadata,
                                 "tray jammed")));
        check(eject_error["media"]["state"] == "EJECT_ERROR");
        check(eject_error["media"]["error"] == "tray jammed");
        check(!snapshot_json_equal_ignoring_revision(serialized, eject_error.dump()));
        check(!snapshot_json_equal_ignoring_revision("{}", serialized));
        const auto empty = nlohmann::json::parse(serialize_daemon_snapshot(
            make_daemon_snapshot(1, {}, MediaLifecycleState::no_disc, std::nullopt, {})));
        check(empty["disc"].is_null() && empty["player"]["track"].is_null());
        std::cout << "PASS: stable daemon snapshot JSON schema\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
