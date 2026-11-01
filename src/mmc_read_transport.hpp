#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <scsi/sg.h>
#include <sys/ioctl.h>

namespace picdplayer_detail {
// Shared only by the two concrete read-only MMC paths. The caller owns fd.
inline int read_mmc_packet(int fd, std::span<const std::uint8_t> command,
                           std::span<std::uint8_t> data, std::span<std::uint8_t> sense,
                           std::size_t& transferred, unsigned timeout_ms) {
    transferred = 0;
    bool valid = false;
    if (command.size() == 10 && command[0] == 0x5a) {
        const auto allocation = (static_cast<unsigned>(command[7]) << 8) | command[8];
        valid = !data.empty() && data.size() <= 512 && allocation == data.size();
    } else if (command.size() == 12 && command[0] == 0xbe) {
        const auto frames = (static_cast<unsigned>(command[6]) << 16) |
            (static_cast<unsigned>(command[7]) << 8) | command[8];
        valid = command[1] == 0x04 && command[9] == 0x12 && command[10] == 0 && command[11] == 0 &&
            frames > 0 && frames <= 75 &&
            data.size() == static_cast<std::size_t>(frames) * (2352 + 294);
    }
    if (!valid || sense.size() > 255 || timeout_ms == 0 || timeout_ms > 5000) return EINVAL;
    sg_io_hdr_t request{};
    request.interface_id = 'S';
    request.dxfer_direction = SG_DXFER_FROM_DEV;
    request.cmd_len = static_cast<unsigned char>(command.size());
    request.cmdp = const_cast<unsigned char*>(command.data());
    request.dxfer_len = static_cast<unsigned>(data.size());
    request.dxferp = data.data();
    request.mx_sb_len = static_cast<unsigned char>(sense.size());
    request.sbp = sense.data();
    request.timeout = timeout_ms;
    if (ioctl(fd, SG_IO, &request) < 0) return errno;
    if (request.status || request.host_status || request.driver_status ||
        (request.info & SG_INFO_OK_MASK) != SG_INFO_OK) return EIO;
    if (request.resid < 0 || static_cast<std::size_t>(request.resid) > data.size()) return EMSGSIZE;
    transferred = data.size() - static_cast<std::size_t>(request.resid);
    return 0;
}
} // namespace picdplayer_detail
