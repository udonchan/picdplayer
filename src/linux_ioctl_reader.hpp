#pragma once
#include "cdda_reader.hpp"
#include "drive_capabilities.hpp"
#include <functional>

// Internal transport seam for tests. Return errno, or zero on full success.
using AudioRead = std::function<int(std::int32_t, std::span<std::int16_t>)>;
struct C2AudioReadResult {
    int error = 0;
    // A successful C2 transport must distinguish a zero-pointer response from
    // one containing at least one reported pointer.
    C2Status c2_status = C2Status::unknown;
};
// Internal C2 packet seam. It returns decoded PCM plus a block-scoped C2
// observation; it must not infer drive-wide C2 trust.
using C2AudioRead = std::function<C2AudioReadResult(std::int32_t,
                                                     std::span<std::int16_t>)>;
// Builds an MMC READ CD transport that requests CD-DA user data and C2 error
// pointers. This remains an internal seam so the CDB and payload parsing can
// be tested without an optical drive.
C2AudioRead make_mmc_c2_audio_read(DrivePacketTransport transport);
class LinuxIoctlReader final : public CddaReader {
public:
    explicit LinuxIoctlReader(AudioRead transport, DirectOptions options = {},
                              C2AudioRead c2_transport = {});
    LinuxIoctlReader(const LinuxIoctlReader&) = delete;
    LinuxIoctlReader& operator=(const LinuxIoctlReader&) = delete;
    void seek(std::int32_t lba) override;
    ReadResult read(std::span<std::int16_t> pcm) override;
private:
    AudioRead transport_;
    DirectOptions options_;
    C2AudioRead c2_transport_;
    std::int32_t cursor_ = 0;
    bool positioned_ = false;
};
