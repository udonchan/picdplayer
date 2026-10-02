#include "presentation_model.hpp"
#include <algorithm>
#include <string_view>

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
std::optional<std::string> bounded_text(std::string_view value) {
    if (value.empty()) return std::nullopt;
    constexpr std::size_t maximum_bytes = 256;
    if (value.size() <= maximum_bytes) return std::string(value);
    value = value.substr(0, maximum_bytes);
    while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xc0) == 0x80)
        value.remove_suffix(1);
    if (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xc0) == 0xc0)
        value.remove_suffix(1);
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}
}

PresentationModel make_presentation_model(std::uint64_t revision, const PlayerState& player,
                                           MediaLifecycleState media, const std::optional<DiscToc>& disc,
                                           const MetadataResult& enrichment, DriveCapabilities drive,
                                           ReadDiagnostics read, std::vector<PlayerEvent> recent_events,
                                           bool has_cover_asset, std::optional<std::uint64_t> disc_generation,
                                           std::optional<std::uint64_t> metadata_generation) {
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
    if (media == MediaLifecycleState::audio_ready && disc && disc_generation && *disc_generation > 0 &&
        metadata_generation && *metadata_generation > 0 &&
        (enrichment.status == MetadataStatus::ambiguous || enrichment.status == MetadataStatus::available) &&
        !result.read.session_id.empty() && !enrichment.candidates.empty() &&
        enrichment.candidates.size() <= 100) {
        PresentationEnrichment::Selection selection;
        selection.session_id = result.read.session_id;
        selection.disc_generation = *disc_generation;
        selection.metadata_generation = *metadata_generation;
        selection.selected_index = enrichment.selected;
        selection.candidates.reserve(enrichment.candidates.size());
        for (std::size_t index = 0; index < enrichment.candidates.size(); ++index) {
            const auto& source = enrichment.candidates[index].metadata;
            selection.candidates.push_back({index, bounded_text(source.album_title),
                bounded_text(source.album_artist), bounded_text(source.country), bounded_text(source.date),
                source.medium_position > 0 ? std::optional<int>(source.medium_position) : std::nullopt,
                bounded_text(source.medium_title), source.tracks.size()});
        }
        result.enrichment.selection = std::move(selection);
    }
    const auto* metadata = selected(enrichment);
    if (metadata) {
        if (!metadata->album_title.empty()) result.disc.title = metadata->album_title;
        if (!metadata->album_artist.empty()) result.disc.artist = metadata->album_artist;
    }
    if (!disc) return result;
    // Only a currently accepted audio TOC can anchor a map.
    // 再取得中や世代不明のTOCを現在の読み取りに結び付けない。
    if (media == MediaLifecycleState::audio_ready && disc_generation && *disc_generation > 0
        && !result.read.session_id.empty() && !disc->tracks.empty()) {
        bool valid = disc->tracks.front().start_lba >= 0;
        for (std::size_t i = 0; i < disc->tracks.size(); ++i) {
            const auto& track = disc->tracks[i];
            const auto end = i + 1 < disc->tracks.size() ? disc->tracks[i + 1].start_lba : disc->leadout_lba;
            valid = valid && track.number > 0 && end > track.start_lba
                && track.length_frames == static_cast<std::int64_t>(end) - track.start_lba;
        }
        if (valid) result.disc.layout = PresentationDiscLayout{*disc, result.read.session_id, *disc_generation};
    }
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
