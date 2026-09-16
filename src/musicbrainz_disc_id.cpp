#include "musicbrainz_disc_id.hpp"
#include <array>
#include <discid/discid.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using DiscHandle = std::unique_ptr<DiscId, decltype(&discid_free)>;

int musicbrainz_offset(std::int32_t lba) {
    constexpr std::int64_t pregap = 150;
    const auto value = std::int64_t{lba} + pregap;
    if (value < 0 || value > std::numeric_limits<int>::max())
        throw std::invalid_argument("TOC offset is outside libdiscid range");
    return static_cast<int>(value);
}

void validate_toc(const DiscToc& toc) {
    if (toc.tracks.empty()) throw std::invalid_argument("empty TOC");
    std::vector<std::int32_t> starts;
    starts.reserve(toc.tracks.size());
    for (const auto& track : toc.tracks) starts.push_back(track.start_lba);
    const auto validated = make_audio_toc(toc.tracks.front().number, starts, toc.leadout_lba);
    if (validated.tracks.size() != toc.tracks.size())
        throw std::invalid_argument("inconsistent TOC");
    for (std::size_t i = 0; i < toc.tracks.size(); ++i) {
        if (toc.tracks[i].number != validated.tracks[i].number ||
            toc.tracks[i].length_frames != validated.tracks[i].length_frames)
            throw std::invalid_argument("inconsistent TOC tracks");
    }
}
}

MusicBrainzDiscId calculate_musicbrainz_disc_id(const DiscToc& toc) {
    validate_toc(toc);
    const int first = toc.tracks.front().number;
    const int last = toc.tracks.back().number;
    std::array<int, 100> offsets{};
    offsets[0] = musicbrainz_offset(toc.leadout_lba);
    for (const auto& track : toc.tracks)
        offsets[static_cast<std::size_t>(track.number)] = musicbrainz_offset(track.start_lba);

    DiscHandle disc(discid_new(), discid_free);
    if (!disc) throw std::runtime_error("libdiscid allocation failed");
    if (!discid_put(disc.get(), first, last, offsets.data()))
        throw std::invalid_argument(std::string("libdiscid rejected TOC: ") +
                                    discid_get_error_msg(disc.get()));
    const char* id = discid_get_id(disc.get());
    const char* toc_text = discid_get_toc_string(disc.get());
    if (!id || !toc_text) throw std::runtime_error("libdiscid returned no result");
    return {id, toc_text};
}
