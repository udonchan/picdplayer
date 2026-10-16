#pragma once
#include "cdda_reader.hpp"
#include <algorithm>
#include <stdexcept>

struct ReadOffsetWindow {
    std::int32_t offset_samples = 0;
    std::uint64_t disc_generation = 0;
    std::int64_t disc_begin_sample = 0, disc_end_sample = 0;
    std::int64_t correctable_begin_sample = 0, correctable_end_sample = 0;
    std::int32_t playable_begin_lba = 0, playable_end_lba = 0;
    bool playable() const { return playable_begin_lba < playable_end_lba; }
};

inline ReadOffsetWindow make_read_offset_window(std::int32_t begin, std::int32_t end,
                                                std::int32_t offset) {
    if (begin < 0 || end <= begin) throw std::invalid_argument("invalid offset audio range");
    ReadOffsetWindow result;
    result.offset_samples = offset;
    result.disc_begin_sample = std::int64_t(begin) * 588;
    result.disc_end_sample = std::int64_t(end) * 588;
    result.correctable_begin_sample = std::clamp(result.disc_begin_sample - offset,
                                                result.disc_begin_sample, result.disc_end_sample);
    result.correctable_end_sample = std::clamp(result.disc_end_sample - offset,
                                              result.disc_begin_sample, result.disc_end_sample);
    result.playable_begin_lba = static_cast<std::int32_t>((result.correctable_begin_sample + 587) / 588);
    result.playable_end_lba = static_cast<std::int32_t>(result.correctable_end_sample / 588);
    return result;
}

std::unique_ptr<CddaReader> make_offset_reader(std::unique_ptr<CddaReader> reader,
                                              ReadOffsetWindow window);
