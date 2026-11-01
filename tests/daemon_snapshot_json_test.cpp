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
        read.requested_strategy = "direct-c2-pointers-single-read";
        read.strategy_downgrade = ReadStrategyDowngrade::c2_probe_pending;
        read.strategy_pending = true;
        read.latest = make_read_evidence({60, 15, 15, ReadStatus::ok, 0, 0});
        read.current_playback = make_read_evidence({45, 15, 15, ReadStatus::ok, 0, 0});
        read.queued_blocks = 8;
        read.buffer_capacity_frames = 300;
        read.startup_buffer_frames = 150;
        read.read_block_frames = 15;
        read.prebuffer_target_frames = 150;
        read.last_prebuffer_wait_ms = 1234;
        read.read_inflight_ms = 234;
        read.read_stall_timeout_ms = 10000;
        read.last_read_stall_ms = 10001;
        read.requested_policy = {ReadVerificationMode::repeat, 75, 2, 3, 10000};
        read.effective_policy = read.requested_policy;
        observe_read(read.stats, {60, 15, 15, ReadStatus::ok, 0, 0});
        DriveCapabilities drive;
        drive.device = "/dev/sr0"; drive.vendor = "ASUS";
        drive.speed_control = {Knowledge::yes, CapabilityEvidenceSource::kernel_reported,
                               "CDROM_GET_CAPABILITY CDC_SELECT_SPEED"};
        drive.requested_speed_x = 4;
        PlayerEvent event;
        event.sequence = 7; event.stream_generation = 3;
        read.stream_generation = 3;
        event.read = *read.latest;
        event.read.read_sequence = 7;
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
        check(json["read"]["strategy"]["requested"] == "direct-c2-pointers-single-read");
        check(json["read"]["strategy"]["downgrade_reason"] == "C2_PROBE_PENDING");
        check(json["read"]["strategy"]["pending"] == true);
        check(json["read"]["latest"]["status"] == "CLEAN");
        check(json["read"]["latest"]["local_verification"] == "SINGLE_READ");
        check(json["read"]["latest"]["read_independence"] == "UNKNOWN");
        check(json["read"]["latest"]["c2_status"] == "NOT_CHECKED");
        check(json["read"]["latest"]["offset_status"] == "UNKNOWN");
        check(json["read"]["latest"]["verification"]["attempts"] == 0);
        check(json["read"]["latest"]["verification"]["overlap"] == "NOT_REQUESTED");
        check(json["read"]["stats"]["read_calls"] == 1);
        check(json["read"]["stats"]["verified_calls"] == 0);
        check(json["read"]["current_playback"]["start_lba"] == 45);
        check(json["read"]["queued_blocks"] == 8);
        check(json["read"]["buffer_capacity_frames"] == 300);
        check(json["read"]["startup_buffer_frames"] == 150);
        check(json["read"]["read_block_frames"] == 15);
        check(json["read"]["prebuffer_target_frames"] == 150);
        check(json["read"]["last_prebuffer_wait_ms"] == 1234);
        check(json["read"]["read_stall"]["inflight_ms"] == 234);
        check(json["read"]["read_stall"]["timeout_ms"] == 10000);
        check(json["read"]["read_stall"]["last_timeout_ms"] == 10001);
        check(json["read"]["policy"]["effective"]["mode"] == "REPEAT");
        check(json["drive"]["vendor"] == "ASUS");
        check(json["drive"]["digital_audio_extraction"]["value"] == "UNKNOWN");
        check(json["drive"]["speed_control"]["value"] == "YES");
        check(json["drive"]["requested_speed_x"] == 4);
        check(json["drive"]["speed_request_error"] == "");
        check(json["drive"]["current_speed_x"].is_null());
        check(json["drive"]["current_read_speed_kbytes_per_second"].is_null());
        check(json["drive"]["current_speed_source"] == "NONE");
        auto reported_drive = drive;
        reported_drive.current_read_speed_kbytes_per_second = 3528;
        reported_drive.current_speed_source = CapabilityEvidenceSource::drive_reported;
        reported_drive.current_speed_detail = "drive report, not measured throughput";
        const auto reported = nlohmann::json::parse(serialize_daemon_snapshot(make_daemon_snapshot(42,
            {PlaybackState::playing, 1, 75}, MediaLifecycleState::audio_ready, toc, metadata,
            {}, read, {event}, reported_drive)));
        check(reported["drive"]["requested_speed_x"] == 4);
        check(reported["drive"]["current_speed_x"].is_null());
        check(reported["drive"]["current_read_speed_kbytes_per_second"] == 3528);
        check(reported["drive"]["current_speed_source"] == "DRIVE_REPORTED");
        check(reported["drive"]["current_speed_detail"] == reported_drive.current_speed_detail);
        check(json["recent_events"][0]["sequence"] == 7);
        check(json["recent_events"][0]["type"] == "READ_OBSERVED");
        check(json["read"]["event_window"]["first_sequence"] == 7);
        auto changed = snapshot;
        changed.read.stream_generation = 4;
        const auto reset = nlohmann::json::parse(serialize_daemon_snapshot(changed));
        check(reset["recent_events"].empty());
        check(reset["read"]["event_window"]["first_sequence"].is_null());
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
        check(json["drive"]["read_offset_samples"].is_null());
        check(json["drive"]["read_offset_source"] == "NONE");
        check(json["read"]["latest"]["offset"].is_null() && json["read"]["offset_window"].is_null());
        auto corrected = snapshot;
        corrected.drive.read_offset_samples = 1;
        corrected.read.offset_window = make_read_offset_window(0, 750, 1);
        corrected.read.offset_window->disc_generation = 5;
        ReadResult offset_read{60, 15, 15, ReadStatus::ok, 0, 0};
        offset_read.offset = OffsetReadEvidence{1, PhysicalReadEvidence{60, 16, 60, 16, 0}};
        corrected.read.latest = make_read_evidence(offset_read);
        auto projection = nlohmann::json::parse(serialize_daemon_snapshot(corrected));
        check(projection["drive"]["read_offset_source"] == "USER_CONFIGURED");
        check(projection["drive"]["read_offset_detail"] == read_offset_detail);
        check(projection["read"]["latest"]["offset_status"] == "CORRECTED");
        check(projection["read"]["latest"]["offset"]["samples"] == 1);
        check(projection["read"]["latest"]["offset"]["single_physical_read"]["frames_requested"] == 16);
        check(projection["read"]["latest"]["frames_read"] == 15);
        check(projection["read"]["offset_window"]["reason"] == "BOUNDARY_CLIPPED");
        check(projection["read"]["offset_window"]["playable"]["end_lba"] == 749);
        check(projection["read"]["offset_window"]["unavailable"][0]["start_sample"] == 440999);
        check(projection["read"]["offset_window"]["alignment_excluded"][0]["start_sample"] == 440412);
        for (int offset : {-1, 0, 441000}) {
            corrected.drive.read_offset_samples = offset;
            corrected.read.offset_window = make_read_offset_window(0, 750, offset);
            projection = nlohmann::json::parse(serialize_daemon_snapshot(corrected));
            const auto& window = projection["read"]["offset_window"];
            if (offset == -1) {
                check(window["playable"]["start_lba"] == 1);
                check(window["unavailable"][0]["end_sample"] == 1);
                check(window["alignment_excluded"][0]["start_sample"] == 1);
            } else if (!offset) {
                check(window["reason"] == "NONE" && window["unavailable"].empty());
                check(window["alignment_excluded"].empty());
            } else check(window["reason"] == "NO_PLAYABLE_RANGE" && window["playable"].is_null());
        }
        offset_read.offset->samples = 0;
        corrected.drive.read_offset_samples = 0;
        corrected.read.offset_window = make_read_offset_window(0, 750, 0);
        offset_read.offset->single_physical_read.reset();
        offset_read.verification.detail_count = maximum_verification_attempts;
        for (auto& detail : offset_read.verification.details) {
            detail.physical_start_lba = 60; detail.physical_frames_requested = 16;
            detail.frames_read = 16; detail.logical_start_lba = 60; detail.logical_frames_requested = 15;
        }
        corrected.read.latest = make_read_evidence(offset_read);
        corrected.read.current_playback = corrected.read.latest;
        corrected.read.active_warning = corrected.read.latest;
        corrected.read.history_included = true;
        corrected.read.recent_reads.assign(read_history_capacity, *corrected.read.latest);
        projection = nlohmann::json::parse(serialize_daemon_snapshot(corrected));
        check(projection.dump().size() < 1024 * 1024);
        check(projection["read"]["history"]["regions"].size() == read_history_capacity);
        check(projection["read"]["latest"]["offset"]["single_physical_read"].is_null());
        check(projection["read"]["latest"]["verification"]["attempt_details"][0]["logical_frames_requested"] == 15);
        check(projection["read"]["latest"]["verification"]["attempt_details"][0]["frames_read"] == 16);
        const DeviceObservationSnapshot observation_change{2,DevicePresence::unknown,DeviceChangeReason::state_changed,"METADATA_UNAVAILABLE",true};
        auto lost=make_daemon_snapshot(99,PlayerState{},MediaLifecycleState::no_disc,std::nullopt,MetadataResult{});
        lost.drive=invalidated_drive_capabilities("/fake",0,observation_change);
        lost.drive.observation_revalidating=true;lost.drive.observation_retiring_io=true;
        const auto observed=nlohmann::json::parse(serialize_daemon_snapshot(lost));
        check(observed["drive"]["observation"]["generation"]==2);
        check(observed["drive"]["observation"]["presence"]=="UNKNOWN");
        check(observed["drive"]["observation"]["reason"]=="STATE_CHANGED");
        check(observed["drive"]["observation"]["retiring_io"]==true);
        check(observed["drive"]["observation"]["method"]=="NODE_SYSFS_POLL");
        check(observed["drive"]["read_offset_samples"]==0);
        check(observed["drive"]["c2_supported"]["value"]=="UNKNOWN");
        check(json["drive"]["observation"].is_null());
        std::cout << "PASS: stable daemon snapshot JSON schema and bounded offset/device projection\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
