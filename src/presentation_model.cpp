#include "presentation_model.hpp"
#include <algorithm>

namespace {
const DiscMetadata* selected(const MetadataResult& result) {
    if (!result.selected || *result.selected >= result.candidates.size()) return nullptr;
    return &result.candidates[*result.selected].metadata;
}
const TrackMetadata* matching_track(const DiscMetadata* metadata, int number) {
    if (!metadata) return nullptr;
    for (const auto& track : metadata->tracks)
        if (track.track_number == number) return &track;
    return nullptr;
}
}

PresentationModel make_presentation_model(std::uint64_t revision, const PlayerState& player,
                                           MediaLifecycleState media, const std::optional<DiscToc>& disc,
                                           const MetadataResult& enrichment, DriveCapabilities drive,
                                           ReadDiagnostics read, std::vector<PlayerEvent> recent_events,
                                           bool has_cover_asset) {
    PresentationModel result;
    result.revision = revision;
    result.player = player;
    result.disc.state = media;
    result.enrichment.status = enrichment.status;
    result.artwork.status = enrichment.artwork.status;
    if (has_cover_asset) result.artwork.cover_url = "/api/presentation/artwork/cover";
    result.drive = std::move(drive);
    result.read = std::move(read);
    result.recent_events = std::move(recent_events);
    const auto* metadata = selected(enrichment);
    if (metadata) {
        if (!metadata->album_title.empty()) result.disc.title = metadata->album_title;
        if (!metadata->album_artist.empty()) result.disc.artist = metadata->album_artist;
    }
    if (!disc) return result;
    result.tracks.reserve(disc->tracks.size());
    for (const auto& track : disc->tracks) {
        PresentationTrack item;
        item.number = track.number;
        item.duration_frames = track.length_frames;
        if (const auto* source = matching_track(metadata, track.number)) {
            if (!source->title.empty()) item.title = source->title;
            if (!source->artist.empty()) item.artist = source->artist;
        }
        result.tracks.push_back(std::move(item));
    }
    if (player.track && player.position_lba) {
        for (const auto& track : disc->tracks) {
            if (track.number != *player.track) continue;
            result.position_frames = std::max<std::int64_t>(0, *player.position_lba - track.start_lba);
            result.track_duration_frames = track.length_frames;
            break;
        }
    }
    return result;
}
