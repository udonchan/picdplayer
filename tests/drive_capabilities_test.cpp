#include "drive_capabilities.hpp"

#include <cerrno>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdarg>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <sys/stat.h>

namespace {
void check(bool value) {
    if (!value) throw std::runtime_error("drive capability test failed");
}

DrivePacketTransport c2_transport(bool supported) {
    return [supported](std::span<const std::uint8_t> command,
                       std::span<std::uint8_t> data,
                       std::span<std::uint8_t> sense) {
        check(command.size() == 10);
        check(command[0] == 0x46 && command[1] == 0x00);
        check(command[2] == 0x00 && command[3] == 0x00);
        check(command[7] == 0x02 && command[8] == 0x00);
        check(data.size() == 512 && !sense.empty());
        data[3] = 12; // Header after data length plus one eight-byte descriptor.
        data[9] = 0x1e;
        data[11] = 4; // CD Read Feature additional length.
        data[12] = supported ? 0x02 : 0x00; // C2 Flags.
        return 0;
    };
}

std::vector<std::uint8_t> audio_page(std::uint8_t flags) {
    std::vector<std::uint8_t> response(34, 0);
    response[1] = 32; // Eight-byte mode header plus a 26-byte capability page.
    response[8] = 0x2a;
    response[9] = 24;
    response[13] = flags;
    return response;
}

DriveModeSenseTransport mode_transport(std::vector<std::uint8_t> response,
                                       std::size_t transferred, int error = 0) {
    return [response = std::move(response), transferred, error](std::span<const std::uint8_t> command,
        std::span<std::uint8_t> data, std::span<std::uint8_t> sense, std::size_t& received) {
        check(command.size() == 10 && command[0] == 0x5a && command[1] == 0x08 && command[2] == 0x2a);
        check(command[3] == 0 && command[7] == 2 && command[8] == 0 && command[9] == 0);
        check(data.size() == 512 && !sense.empty() && response.size() <= data.size());
        std::copy(response.begin(), response.end(), data.begin());
        received = transferred;
        return error;
    };
}
unsigned ioctl_scenario = 0, mode_ioctl_calls = 0;
int mode_fd = -1;
} // namespace

// Only this test executable wraps ioctl. File descriptors are regular temp
// files, and no request is forwarded to a physical device.
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    struct stat info{};
    check(fstat(fd, &info) == 0 && S_ISREG(info.st_mode));
    check((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
    if (request == CDROM_GET_CAPABILITY) return CDC_SELECT_SPEED;
    va_list args;
    va_start(args, request);
    if (request == CDROM_SEND_PACKET) {
        auto* packet = va_arg(args, cdrom_generic_command*);
        va_end(args);
        check(packet->cmd[0] == 0x46 && packet->buflen == 512 && packet->data_direction == CGC_DATA_READ);
        packet->buffer[3] = 12;
        packet->buffer[9] = 0x1e;
        packet->buffer[11] = 4;
        packet->buffer[12] = 2;
        return 0;
    }
    if (request == SG_IO) {
        auto* packet = va_arg(args, sg_io_hdr_t*);
        va_end(args);
        check(packet->interface_id == 'S' && packet->dxfer_direction == SG_DXFER_FROM_DEV);
        check(packet->cmd_len == 10 && packet->cmdp[0] == 0x5a && packet->cmdp[1] == 8 && packet->cmdp[2] == 0x2a);
        check(packet->dxfer_len == 512 && packet->timeout == 1500 && packet->mx_sb_len > 0);
        const auto response = audio_page(3);
        std::copy(response.begin(), response.end(), static_cast<std::uint8_t*>(packet->dxferp));
        packet->resid = static_cast<int>(packet->dxfer_len - response.size());
        ++mode_ioctl_calls;
        mode_fd = fd;
        if (ioctl_scenario == 1) packet->status = 2;
        if (ioctl_scenario == 2) packet->resid = -1;
        if (ioctl_scenario == 3) packet->resid = 513;
        if (ioctl_scenario == 4) { errno = EACCES; return -1; }
        if (ioctl_scenario == 5) packet->driver_status = 8;
        if (ioctl_scenario == 6) packet->host_status = 1;
        if (ioctl_scenario == 7) packet->info = SG_INFO_CHECK;
        return 0;
    }
    va_end(args);
    errno = ENOTTY;
    return -1;
}

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / "picdplayer-drive-capability-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "fake" / "device");
        { std::ofstream(root / "fake" / "device" / "vendor") << " VENDOR \n";
          std::ofstream(root / "fake" / "device" / "model") << "MODEL\n";
          std::ofstream(root / "fake" / "device" / "rev") << "1.0\n"; }

        unsigned requested_speed = 0;
        const auto requested = request_drive_speed(4, [&](unsigned speed_x) {
            requested_speed = speed_x;
            return 0;
        });
        check(requested_speed == 4 && requested.requested_speed_x == 4 && requested.error.empty());

        const auto rejected_speed = request_drive_speed(0, [](unsigned) { return 0; });
        check(!rejected_speed.requested_speed_x && !rejected_speed.error.empty());
        const auto denied_speed = request_drive_speed(8, [](unsigned) { return EPERM; });
        check(!denied_speed.requested_speed_x && denied_speed.error.find("CDROM_SELECT_SPEED") != std::string::npos);

        const auto unavailable = probe_drive_capabilities("/nonexistent/fake", root);
        check(unavailable.vendor == "VENDOR" && unavailable.model == "MODEL" && unavailable.firmware == "1.0");
        check(unavailable.digital_audio_extraction.value == Knowledge::unknown);
        check(unavailable.c2_supported.value == Knowledge::unknown);
        check(unavailable.speed_control.value == Knowledge::unknown);
        check(!unavailable.probe_error.empty());

        const auto c2_yes = probe_drive_capabilities("/synthetic/fake", root, c2_transport(true));
        check(c2_yes.c2_supported.value == Knowledge::yes);
        check(c2_yes.c2_supported.source == CapabilityEvidenceSource::drive_reported);
        check(c2_yes.c2_supported.detail == "MMC GET CONFIGURATION CD Read feature C2 Flags");
        check(c2_yes.speed_control.value == Knowledge::unknown);
        check(c2_yes.probe_error.empty());

        const auto c2_no = probe_drive_capabilities("/synthetic/fake", root, c2_transport(false));
        check(c2_no.c2_supported.value == Knowledge::no);
        check(c2_no.c2_supported.source == CapabilityEvidenceSource::drive_reported);

        const auto absent = probe_drive_capabilities(
            "/synthetic/fake", root,
            [](std::span<const std::uint8_t>, std::span<std::uint8_t> data, std::span<std::uint8_t>) {
                data[3] = 4; // Configuration header, no feature descriptors.
                return 0;
            });
        check(absent.c2_supported.value == Knowledge::no);
        check(absent.c2_supported.source == CapabilityEvidenceSource::drive_reported);
        check(absent.c2_supported.detail == "MMC GET CONFIGURATION CD Read feature absent");

        const auto malformed = probe_drive_capabilities(
            "/synthetic/fake", root,
            [](std::span<const std::uint8_t>, std::span<std::uint8_t> data, std::span<std::uint8_t>) {
                data[3] = 5;
                return 0;
            });
        check(malformed.c2_supported.value == Knowledge::unknown);
        check(!malformed.probe_error.empty());

        const auto denied = probe_drive_capabilities(
            "/synthetic/fake", root,
            [](std::span<const std::uint8_t>, std::span<std::uint8_t>, std::span<std::uint8_t>) { return EPERM; });
        check(denied.c2_supported.value == Knowledge::unknown);
        check(!denied.probe_error.empty());

        unsigned mode_cases = 0;
        const auto reports = [&](const std::vector<std::uint8_t>& data, std::size_t received, int error = 0) {
            ++mode_cases;
            return probe_drive_capabilities("/synthetic/fake", root, c2_transport(true),
                mode_transport(data, received, error));
        };
        const auto unknown = [&](const std::vector<std::uint8_t>& data, std::size_t received, int error = 0) {
            const auto result = reports(data, received, error);
            check(result.digital_audio_extraction.value == Knowledge::unknown);
            check(result.accurate_stream.value == Knowledge::unknown);
            check(result.digital_audio_extraction.source == CapabilityEvidenceSource::none);
            check(!result.digital_audio_extraction.detail.empty() && !result.accurate_stream.detail.empty());
            check(result.c2_supported.value == Knowledge::yes && result.probe_error.empty());
        };
        for (unsigned flags = 0; flags < 4; ++flags) {
            const auto data = audio_page(static_cast<std::uint8_t>(flags));
            const auto result = reports(data, data.size());
            check(result.digital_audio_extraction.value == (flags & 1 ? Knowledge::yes : Knowledge::no));
            check(result.accurate_stream.value == (flags & 2 ? Knowledge::yes : Knowledge::no));
            check(result.digital_audio_extraction.source == CapabilityEvidenceSource::drive_reported);
            check(result.accurate_stream.source == CapabilityEvidenceSource::drive_reported);
            check(result.c2_trustworthy.value == Knowledge::unknown);
        }
        auto analog = audio_page(0);
        analog[12] = 1; // Audio play, not CDDA_CS.
        check(reports(analog, analog.size()).digital_audio_extraction.value == Knowledge::no);
        const auto valid = audio_page(3);
        for (std::size_t received = 0; received < valid.size(); ++received) unknown(valid, received);
        unknown(valid, 513);
        for (const auto error : {EACCES, EPERM, EIO, ENOTSUP}) unknown(valid, valid.size(), error);
        auto malformed_mode = valid;
        malformed_mode[1] = 0;
        unknown(malformed_mode, malformed_mode.size());
        malformed_mode[0] = 255;
        malformed_mode[1] = 255;
        unknown(malformed_mode, malformed_mode.size());
        malformed_mode = valid;
        malformed_mode[7] = 27;
        unknown(malformed_mode, malformed_mode.size());
        auto descriptors = valid;
        descriptors.insert(descriptors.begin() + 8, 8, 0);
        descriptors[1] = static_cast<std::uint8_t>(descriptors.size() - 2);
        descriptors[7] = 8;
        check(reports(descriptors, descriptors.size()).digital_audio_extraction.value == Knowledge::yes);
        auto saved = valid;
        saved[8] |= 0x80; // PS does not change requested PC=current.
        check(reports(saved, saved.size()).accurate_stream.value == Knowledge::yes);
        for (const unsigned rate : {0U, 1U, 3528U, 65535U}) {
            auto speed = valid;
            speed[16] = 0xff; speed[17] = 0xff; // Maximum field must never substitute for current.
            speed[22] = static_cast<std::uint8_t>(rate >> 8);
            speed[23] = static_cast<std::uint8_t>(rate);
            const auto result = reports(speed, speed.size());
            check(result.current_read_speed_kbytes_per_second == static_cast<int>(rate));
            check(result.current_speed_source == CapabilityEvidenceSource::drive_reported);
            check(!result.current_speed_x && !result.current_speed_detail.empty());
        }
        auto missing = valid;
        missing[8] = 0; // A bounded vendor-specific page is not the requested page.
        unknown(missing, missing.size());
        auto subpage = valid;
        subpage[8] |= 0x40;
        subpage[9] = 0;
        subpage[10] = 0;
        subpage[11] = 22;
        unknown(subpage, subpage.size());
        auto duplicate_page = valid;
        duplicate_page.insert(duplicate_page.end(), valid.begin() + 8, valid.end());
        duplicate_page[1] = static_cast<std::uint8_t>(duplicate_page.size() - 2);
        unknown(duplicate_page, duplicate_page.size());
        for (unsigned size = 0; size < 4; ++size) {
            auto short_page = valid;
            short_page.resize(10 + size);
            short_page[1] = static_cast<std::uint8_t>(short_page.size() - 2);
            short_page[9] = static_cast<std::uint8_t>(size);
            unknown(short_page, short_page.size());
        }
        auto minimum_page = valid;
        minimum_page.resize(14); // Structural field boundary: six bytes of page, not a full MMC conformance claim.
        minimum_page[1] = 12;
        minimum_page[9] = 4;
        check(reports(minimum_page, minimum_page.size()).digital_audio_extraction.value == Knowledge::yes);
        const auto missing_speed = reports(minimum_page, minimum_page.size());
        check(!missing_speed.current_read_speed_kbytes_per_second);
        check(missing_speed.current_speed_source == CapabilityEvidenceSource::none);
        check(!missing_speed.current_speed_detail.empty());
        auto other_subpage = valid;
        other_subpage.insert(other_subpage.end(), {0x4c, 1, 0, 1, 0xaa});
        other_subpage[1] = static_cast<std::uint8_t>(other_subpage.size() - 2);
        check(reports(other_subpage, other_subpage.size()).accurate_stream.value == Knowledge::yes);
        auto short_subpage = valid;
        short_subpage.insert(short_subpage.end(), {0x4c, 1, 0});
        short_subpage[1] = static_cast<std::uint8_t>(short_subpage.size() - 2);
        unknown(short_subpage, short_subpage.size());
        short_subpage = valid;
        short_subpage.insert(short_subpage.end(), {0x4c, 1, 0, 2, 0xaa});
        short_subpage[1] = static_cast<std::uint8_t>(short_subpage.size() - 2);
        unknown(short_subpage, short_subpage.size());
        auto extra_page = valid;
        extra_page.push_back(0);
        extra_page.push_back(0);
        extra_page[1] = static_cast<std::uint8_t>(extra_page.size() - 2);
        check(reports(extra_page, extra_page.size()).accurate_stream.value == Knowledge::yes);
        extra_page.push_back(0); // Incomplete trailing header must not publish the earlier valid page.
        extra_page[1] = static_cast<std::uint8_t>(extra_page.size() - 2);
        unknown(extra_page, extra_page.size());
        const auto independent = probe_drive_capabilities("/synthetic/fake", root,
            [](auto, auto, auto) { return EPERM; }, mode_transport(valid, valid.size()));
        check(independent.c2_supported.value == Knowledge::unknown && !independent.probe_error.empty());
        check(independent.digital_audio_extraction.value == Knowledge::yes);
        check(independent.accurate_stream.value == Knowledge::yes);
        const auto no_mode = probe_drive_capabilities("/synthetic/fake", root, c2_transport(true), {});
        check(no_mode.digital_audio_extraction.value == Knowledge::unknown);
        check(!no_mode.digital_audio_extraction.detail.empty());

        const auto node = root / "mode-node";
        { std::ofstream out(node); check(static_cast<bool>(out)); }
        for (ioctl_scenario = 0; ioctl_scenario < 8; ++ioctl_scenario) {
            const auto result = probe_drive_capabilities(node.string(), root);
            check(result.speed_control.value == Knowledge::yes && result.c2_supported.value == Knowledge::yes);
            check(result.digital_audio_extraction.value == (ioctl_scenario == 0 ? Knowledge::yes : Knowledge::unknown));
            check(result.accurate_stream.value == result.digital_audio_extraction.value);
            check(result.probe_error.empty() && !result.digital_audio_extraction.detail.empty());
            check(fcntl(mode_fd, F_GETFD) == -1 && errno == EBADF);
        }
        check(mode_ioctl_calls == 8);

        std::filesystem::remove_all(root);
        std::cout << "PASS: C2/speed compatibility, " << mode_cases
                  << " bounded CDDA mode reports and 8 mocked SG_IO outcomes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
