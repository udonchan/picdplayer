#pragma once
#include <cstdint>
#include <span>
#include <vector>

inline constexpr int cd_frames_per_second = 75;

// Audio-only CD model. Positions and lengths are CD sectors (1/75 second).
struct Track {
    int number;
    std::int32_t start_lba;
    std::int64_t length_frames;
};
struct DiscToc {
    std::vector<Track> tracks;
    std::int32_t leadout_lba;
    std::int64_t span_frames() const;
};

// Validates the complete snapshot; throws invalid_argument for invalid input.
// Track numbers are consecutive, starting at first_track (not necessarily 1).
DiscToc make_audio_toc(int first_track, std::span<const std::int32_t> starts,
                       std::int32_t leadout_lba);
