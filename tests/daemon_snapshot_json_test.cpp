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
        const auto snapshot = make_daemon_snapshot(42,
            {PlaybackState::playing, 1, 75}, MediaLifecycleState::audio_ready, toc, metadata);
        const auto json = nlohmann::json::parse(serialize_daemon_snapshot(snapshot));
        check(json["revision"] == 42 && json["player"]["state"] == "PLAYING");
        check(json["player"]["position_in_track_frames"] == 75);
        check(json["disc"]["tracks"][0]["length_frames"] == 750);
        check(json["metadata"]["status"] == "AVAILABLE" && json["metadata"]["selected"] == 0);
        check(json["metadata"]["candidates"][0]["tracks"][0]["title"] == "Song");
        check(json["metadata"]["cover_art"]["status"] == "AVAILABLE");
        const auto eject_error = nlohmann::json::parse(serialize_daemon_snapshot(
            make_daemon_snapshot(43, {PlaybackState::playing, 1, 75},
                                 MediaLifecycleState::eject_error, toc, metadata,
                                 "tray jammed")));
        check(eject_error["media"]["state"] == "EJECT_ERROR");
        check(eject_error["media"]["error"] == "tray jammed");
        const auto empty = nlohmann::json::parse(serialize_daemon_snapshot(
            make_daemon_snapshot(1, {}, MediaLifecycleState::no_disc, std::nullopt, {})));
        check(empty["disc"].is_null() && empty["player"]["track"].is_null());
        std::cout << "PASS: stable daemon snapshot JSON schema\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
