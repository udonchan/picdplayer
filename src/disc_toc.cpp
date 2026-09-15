#include "disc_toc.hpp"
#include <stdexcept>

std::int64_t DiscToc::span_frames() const {
    return tracks.empty() ? 0 : std::int64_t(leadout_lba) - tracks.front().start_lba;
}

DiscToc make_audio_toc(int first_track, std::span<const std::int32_t> starts,
                       std::int32_t leadout_lba) {
    if (first_track < 1 || first_track > 99 || starts.empty() ||
        starts.size() > static_cast<std::size_t>(100 - first_track))
        throw std::invalid_argument("invalid TOC track range");
    DiscToc toc{{}, leadout_lba};
    toc.tracks.reserve(starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto end = i + 1 < starts.size() ? starts[i + 1] : leadout_lba;
        if (starts[i] < 0 || end <= starts[i])
            throw std::invalid_argument("TOC positions must be nonnegative and strictly increasing");
        toc.tracks.push_back({first_track + static_cast<int>(i), starts[i],
                              std::int64_t(end) - starts[i]});
    }
    return toc;
}
