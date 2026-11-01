#include "drive_capabilities.hpp"
#include "mmc_read_transport.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/cdrom.h>
#include <scsi/sg.h>
#include <sstream>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::uint8_t get_configuration = 0x46;
constexpr std::uint16_t cd_read_feature = 0x001e;
constexpr std::size_t configuration_response_bytes = 512;
constexpr std::size_t configuration_header_bytes = 8;
constexpr std::size_t feature_header_bytes = 4;
constexpr unsigned maximum_requested_speed_x = 255;
constexpr std::size_t mode_response_bytes = 512;
constexpr unsigned mode_timeout_ms = 1500;

std::string read_optional(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value)) return {};
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::uint16_t be16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0]) << 8 | value[1];
}

DriveCapabilities identity_from_sysfs(const std::string& device,
                                      const std::filesystem::path& sysfs_root) {
    DriveCapabilities result;
    result.device = device;
    const auto name = std::filesystem::path(device).filename();
    const auto base = sysfs_root / name / "device";
    result.vendor = read_optional(base / "vendor");
    result.model = read_optional(base / "model");
    result.firmware = read_optional(base / "rev");
    return result;
}

DrivePacketTransport linux_packet_transport(const std::string& device) {
    return [device](std::span<const std::uint8_t> command, std::span<std::uint8_t> data,
                    std::span<std::uint8_t> sense) -> int {
        if (command.size() > CDROM_PACKET_SIZE) return EINVAL;
        const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return errno;

        request_sense request_sense_data{};
        cdrom_generic_command request{};
        std::memcpy(request.cmd, command.data(), command.size());
        request.buffer = data.data();
        request.buflen = static_cast<unsigned int>(data.size());
        request.sense = &request_sense_data;
        request.data_direction = CGC_DATA_READ;
        request.quiet = 1;
        request.timeout = 5000;
        const int result = ioctl(fd, CDROM_SEND_PACKET, &request);
        const int error = result < 0 ? errno : 0;
        const auto sense_bytes = sizeof(request_sense_data) < sense.size()
            ? sizeof(request_sense_data) : sense.size();
        std::memcpy(sense.data(), &request_sense_data, sense_bytes);
        close(fd);
        return error;
    };
}

DriveReadPacketTransport linux_mode_transport(const std::string& device) {
    return [device](std::span<const std::uint8_t> command, std::span<std::uint8_t> data,
                    std::span<std::uint8_t> sense, std::size_t& transferred) -> int {
        transferred = 0;
        if (command.size() != 10 || command[0] != 0x5a || data.empty() ||
            data.size() > mode_response_bytes || sense.size() > 255) return EINVAL;
        const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return errno;
        const auto error = picdplayer_detail::read_mmc_packet(fd, command, data, sense, transferred, mode_timeout_ms);
        close(fd);
        return error;
    };
}

struct ModePageResult {
    std::vector<std::uint8_t> page;
    std::string error;
};

ModePageResult read_mode_page(std::uint8_t requested, const DriveReadPacketTransport& transport) {
    const auto unavailable = [](std::string reason) -> ModePageResult { return {{}, std::move(reason)}; };
    if (!transport) return unavailable("transport unavailable");
    std::array<std::uint8_t, 10> command{};
    command[0] = 0x5a;
    command[1] = 0x08; // DBD, page control=current, subpage=0.
    command[2] = requested;
    command[7] = static_cast<std::uint8_t>(mode_response_bytes >> 8);
    command[8] = static_cast<std::uint8_t>(mode_response_bytes);
    std::array<std::uint8_t, mode_response_bytes> data{};
    std::array<std::uint8_t, sizeof(request_sense)> sense{};
    std::size_t transferred = 0;
    const auto error = transport(command, data, sense, transferred);
    if (error) return unavailable(std::strerror(error));
    if (transferred < 8 || transferred > data.size()) return unavailable("transfer length invalid");
    const auto total = static_cast<std::size_t>(be16(data.data())) + 2;
    if (total < 8 || total > transferred) return unavailable("response header is truncated");
    const auto descriptors = static_cast<std::size_t>(be16(data.data() + 6));
    if (descriptors > total - 8) return unavailable("block descriptors are truncated");
    std::span<const std::uint8_t> page;
    for (std::size_t offset = 8 + descriptors; offset < total;) {
        if (total - offset < 2) return unavailable("page header is truncated");
        const bool subpage = (data[offset] & 0x40) != 0;
        const std::size_t header = subpage ? 4 : 2;
        if (total - offset < header) return unavailable("subpage header is truncated");
        const auto length = header + (subpage ? static_cast<std::size_t>(be16(data.data() + offset + 2))
                                             : static_cast<std::size_t>(data[offset + 1]));
        if (length > total - offset) return unavailable("page payload is truncated");
        if ((data[offset] & 0x3f) == requested) {
            if (!page.empty() || subpage) return unavailable("ambiguous requested page layout");
            page = std::span<const std::uint8_t>(data.data() + offset, length);
        }
        offset += length;
    }
    if (page.empty()) return unavailable("requested page not returned");
    return {std::vector<std::uint8_t>(page.begin(), page.end()), {}};
}

void probe_cd_audio_report(DriveCapabilities& result, const DriveReadPacketTransport& transport) {
    const auto response = read_mode_page(0x2a, transport);
    const auto& page = response.page;
    if (!response.error.empty() || page.size() < 6) {
        const auto detail = "MMC MODE SENSE(10) CD/DVD page: " +
            (response.error.empty() ? "CD/DVD flags not returned" : response.error);
        result.digital_audio_extraction.detail = detail;
        result.accurate_stream.detail = detail;
        result.current_speed_detail = detail;
        return;
    }
    result.digital_audio_extraction = {page[5] & 1 ? Knowledge::yes : Knowledge::no,
        CapabilityEvidenceSource::drive_reported, "MMC CD/DVD page CDDA_CS (digital commands, not Audio play)"};
    result.accurate_stream = {page[5] & 2 ? Knowledge::yes : Knowledge::no,
        CapabilityEvidenceSource::drive_reported, "MMC CD/DVD page CDDA_SA (drive report, not measured continuity)"};
    if (page.size() < 16) {
        result.current_speed_detail = "MMC CD/DVD page current-read-speed field not returned";
    } else {
        const auto rate = be16(page.data() + 14);
        result.current_read_speed_kbytes_per_second = rate;
        result.current_speed_source = CapabilityEvidenceSource::drive_reported;
        result.current_speed_detail = rate == 0 ?
            "MMC current read speed reported zero; no physical speed inferred" :
            "MMC current read speed at capability probe, kB/s report; not spindle/throughput measurement or CD/DVD x";
    }
}

void probe_cache_report(DriveCapabilities& result, const DriveReadPacketTransport& transport) {
    const auto response = read_mode_page(0x08, transport);
    if (!response.error.empty() || response.page.size() < 3) {
        result.read_cache.detail = "MMC MODE SENSE(10) caching page: " +
            (response.error.empty() ? "RCD field not returned" : response.error);
        return;
    }
    const bool disabled = (response.page[2] & 1) != 0;
    result.read_cache = {disabled ? Knowledge::no : Knowledge::yes,
        CapabilityEvidenceSource::drive_reported,
        disabled ? "Current caching policy RCD=1: reported disabled; not hardware-cache absence or read independence" :
                   "Current caching policy RCD=0: reported enabled; not hardware-cache size or read independence"};
}

void probe_c2_feature(DriveCapabilities& result, const DrivePacketTransport& transport) {
    std::array<std::uint8_t, 10> command{};
    command[0] = get_configuration;
    // RT=all features. Some USB bridges do not honor a single-feature request;
    // scanning the complete descriptor list avoids treating that quirk as C2 absence.
    command[7] = static_cast<std::uint8_t>(configuration_response_bytes >> 8);
    command[8] = static_cast<std::uint8_t>(configuration_response_bytes);
    std::array<std::uint8_t, configuration_response_bytes> data{};
    std::array<std::uint8_t, sizeof(request_sense)> sense{};
    const int error = transport(command, data, sense);
    if (error) {
        result.probe_error = std::system_error(error, std::generic_category(),
                                               "GET CONFIGURATION " + result.device).what();
        return;
    }

    const auto reported = static_cast<std::size_t>(
        (static_cast<std::uint32_t>(be16(data.data())) << 16) | be16(data.data() + 2));
    const auto total = reported + 4;
    if (reported < 4 || total > data.size()) {
        std::ostringstream message;
        message << "GET CONFIGURATION response is malformed (data_length=" << reported << ')';
        result.probe_error = message.str();
        return;
    }

    for (std::size_t offset = configuration_header_bytes; offset < total;) {
        if (offset + feature_header_bytes > total) {
            result.probe_error = "GET CONFIGURATION feature descriptor header is truncated";
            return;
        }
        const auto additional_length = static_cast<std::size_t>(data[offset + 3]);
        const auto descriptor_bytes = feature_header_bytes + additional_length;
        if (offset + descriptor_bytes > total) {
            result.probe_error = "GET CONFIGURATION feature descriptor is truncated";
            return;
        }
        if (be16(data.data() + offset) == cd_read_feature) {
            if (additional_length < 1) {
                result.probe_error = "GET CONFIGURATION CD Read feature is truncated";
                return;
            }
            result.c2_supported.value = data[offset + feature_header_bytes] & 0x02
                ? Knowledge::yes : Knowledge::no;
            result.c2_supported.source = CapabilityEvidenceSource::drive_reported;
            result.c2_supported.detail = "MMC GET CONFIGURATION CD Read feature C2 Flags";
            return;
        }
        offset += descriptor_bytes;
    }

    result.c2_supported.value = Knowledge::no;
    result.c2_supported.source = CapabilityEvidenceSource::drive_reported;
    result.c2_supported.detail = "MMC GET CONFIGURATION CD Read feature absent";
}
bool probe_kernel_capabilities(DriveCapabilities& result) {
    const int fd = open(result.device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        result.probe_error = std::system_error(errno, std::generic_category(),
                                               "open " + result.device).what();
        return false;
    }
    const int capabilities = ioctl(fd, CDROM_GET_CAPABILITY, 0);
    const int error = errno;
    close(fd);
    if (capabilities < 0) {
        result.probe_error = std::system_error(error, std::generic_category(),
                                               "CDROM_GET_CAPABILITY " + result.device).what();
        return false;
    }
    result.speed_control.value = capabilities & CDC_SELECT_SPEED ? Knowledge::yes : Knowledge::no;
    result.speed_control.source = CapabilityEvidenceSource::kernel_reported;
    result.speed_control.detail = "CDROM_GET_CAPABILITY CDC_SELECT_SPEED";
    return true;
}
DriveSpeedTransport linux_speed_transport(const std::string& device) {
    return [device](unsigned speed_x) -> int {
        const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return errno;
        const int result = ioctl(fd, CDROM_SELECT_SPEED, static_cast<unsigned long>(speed_x));
        const int error = result < 0 ? errno : 0;
        close(fd);
        return error;
    };
}
} // namespace

DriveSpeedRequestResult request_drive_speed(unsigned speed_x,
                                            const DriveSpeedTransport& transport) {
    if (speed_x == 0 || speed_x > maximum_requested_speed_x)
        return {{}, "requested CD speed must be in the range 1..255x"};
    if (!transport) return {{}, "CDROM_SELECT_SPEED transport is unavailable"};
    const int error = transport(speed_x);
    if (error)
        return {{}, std::system_error(error, std::generic_category(),
                                      "CDROM_SELECT_SPEED").what()};
    return {static_cast<int>(speed_x), {}};
}

DriveSpeedRequestResult request_drive_speed(const std::string& device, unsigned speed_x) {
    return request_drive_speed(speed_x, linux_speed_transport(device));
}

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root) {
    auto result = identity_from_sysfs(device, sysfs_root);
    if (!probe_kernel_capabilities(result)) {
        result.digital_audio_extraction.detail = "Not queried: " + result.probe_error;
        result.accurate_stream.detail = result.digital_audio_extraction.detail;
        result.current_speed_detail = result.digital_audio_extraction.detail;
        result.read_cache.detail = result.digital_audio_extraction.detail;
        return result;
    }
    probe_c2_feature(result, linux_packet_transport(device));
    probe_cd_audio_report(result, linux_mode_transport(device));
    probe_cache_report(result, linux_mode_transport(device));
    return result;
}

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root,
                                            const DrivePacketTransport& packet_transport) {
    auto result = identity_from_sysfs(device, sysfs_root);
    probe_c2_feature(result, packet_transport);
    return result;
}

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root,
                                            const DrivePacketTransport& packet_transport,
                                            const DriveReadPacketTransport& mode_transport) {
    auto result = identity_from_sysfs(device, sysfs_root);
    probe_c2_feature(result, packet_transport);
    probe_cd_audio_report(result, mode_transport);
    probe_cache_report(result, mode_transport);
    return result;
}

const char* knowledge_name(Knowledge value) {
    switch (value) {
    case Knowledge::unknown: return "UNKNOWN";
    case Knowledge::no: return "NO";
    case Knowledge::yes: return "YES";
    }
    return "UNKNOWN";
}

const char* capability_evidence_source_name(CapabilityEvidenceSource value) {
    switch (value) {
    case CapabilityEvidenceSource::none: return "NONE";
    case CapabilityEvidenceSource::kernel_reported: return "KERNEL_REPORTED";
    case CapabilityEvidenceSource::drive_reported: return "DRIVE_REPORTED";
    case CapabilityEvidenceSource::tested: return "TESTED";
    case CapabilityEvidenceSource::database: return "DATABASE";
    case CapabilityEvidenceSource::user_configured: return "USER_CONFIGURED";
    case CapabilityEvidenceSource::inferred: return "INFERRED";
    }
    return "NONE";
}
