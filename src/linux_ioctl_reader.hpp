#pragma once
#include "cdda_reader.hpp"
#include <functional>

// Internal transport seam for tests. Return errno, or zero on full success.
using AudioRead = std::function<int(std::int32_t, std::span<std::int16_t>)>;
class LinuxIoctlReader final : public CddaReader {
public:
    explicit LinuxIoctlReader(AudioRead transport, DirectOptions options = {});
    LinuxIoctlReader(const LinuxIoctlReader&) = delete;
    LinuxIoctlReader& operator=(const LinuxIoctlReader&) = delete;
    void seek(std::int32_t lba) override;
    ReadResult read(std::span<std::int16_t> pcm) override;
private:
    AudioRead transport_;
    DirectOptions options_;
    std::int32_t cursor_ = 0;
    bool positioned_ = false;
};
