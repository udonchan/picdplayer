#include "daemon_snapshot.hpp"
#include <stdexcept>

DaemonSnapshot make_daemon_snapshot(std::uint64_t revision,
                                    const PlayerState& player,
                                    MediaLifecycleState media,
                                    const std::optional<DiscToc>& disc,
                                    const MetadataResult& metadata) {
    DaemonSnapshot snapshot{revision, player, media, disc, metadata, std::nullopt, std::nullopt};
    if (player.playback == PlaybackState::no_disc) {
        if (player.track || player.position_lba)
            throw std::invalid_argument("NO_DISC player snapshot contains a position");
        return snapshot;
    }
    if (!disc) throw std::invalid_argument("player snapshot has no TOC");
    if (!player.track || !player.position_lba)
        throw std::invalid_argument("player snapshot is missing track or position");
    for (const auto& track : disc->tracks) {
        if (track.number != *player.track) continue;
        const auto relative = std::int64_t{*player.position_lba} - track.start_lba;
        if (relative < 0 || relative >= track.length_frames)
            throw std::invalid_argument("player position is outside current track");
        snapshot.position_in_track_frames = relative;
        snapshot.current_track_length_frames = track.length_frames;
        return snapshot;
    }
    throw std::invalid_argument("player track is absent from TOC");
}
