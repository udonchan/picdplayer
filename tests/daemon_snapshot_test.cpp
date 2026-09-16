#include "daemon_snapshot.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("daemon snapshot test failed"); }
template<class Function> void invalid(Function function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("inconsistent owner snapshots accepted");
}
}

int main() {
    try {
        MetadataResult metadata;
        auto empty = make_daemon_snapshot(1, {}, MediaLifecycleState::no_disc, std::nullopt, metadata);
        check(empty.revision == 1 && !empty.disc && !empty.position_in_track_frames);

        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0, 750}, 1500);
        PlayerState player{PlaybackState::playing, 2, 900};
        metadata.status = MetadataStatus::available;
        metadata.disc_id = "disc-id";
        DiscMetadata selected;
        selected.release_id = "release-id";
        selected.album_title = "Album";
        metadata.candidates.push_back({std::move(selected)});
        metadata.selected = 0;
        auto snapshot = make_daemon_snapshot(7, player, MediaLifecycleState::audio_ready, toc, metadata);
        check(snapshot.revision == 7 && snapshot.disc->tracks.size() == 2);
        check(snapshot.position_in_track_frames == 150);
        check(snapshot.current_track_length_frames == 750);
        check(snapshot.metadata.selected == 0 && snapshot.metadata.candidates[0].metadata.album_title == "Album");

        invalid([&] { (void)make_daemon_snapshot(8, player, MediaLifecycleState::audio_ready, std::nullopt, metadata); });
        player.position_lba = 1500;
        invalid([&] { (void)make_daemon_snapshot(9, player, MediaLifecycleState::audio_ready, toc, metadata); });
        std::cout << "PASS: daemon snapshot projection and consistency checks\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
