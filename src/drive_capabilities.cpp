#include "drive_capabilities.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/cdrom.h>
#include <sstream>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace {
constexpr std::uint8_t get_configuration = 0x46;
constexpr std::uint16_t cd_read_feature = 0x001e;
constexpr std::size_t configuration_response_bytes = 12;

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

void probe_c2_feature(DriveCapabilities& result, const DrivePacketTransport& transport) {
    std::array<std::uint8_t, 10> command{};
    command[0] = get_configuration;
    command[1] = 0x02; // RT=single feature
    command[2] = static_cast<std::uint8_t>(cd_read_feature >> 8);
    command[3] = static_cast<std::uint8_t>(cd_read_feature);
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
    if (reported < 8 || reported + 4 > data.size() ||
        be16(data.data() + 4) != cd_read_feature || data[7] != 4) {
        result.probe_error = "GET CONFIGURATION CD Read feature response is malformed or absent";
        return;
    }
    result.c2_supported.value = data[8] & 0x02 ? Knowledge::yes : Knowledge::no;
    result.c2_supported.source = CapabilityEvidenceSource::drive_reported;
    result.c2_supported.detail = "MMC GET CONFIGURATION CD Read feature C2 Flags";
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
} // namespace

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root) {
    auto result = identity_from_sysfs(device, sysfs_root);
    if (!probe_kernel_capabilities(result)) return result;
    probe_c2_feature(result, linux_packet_transport(device));
    // CDC_PLAY_AUDIO concerns the drive's analogue/play command interface and
    // must not be presented as proof of digital audio extraction support.
    return result;
}

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root,
                                            const DrivePacketTransport& packet_transport) {
    auto result = identity_from_sysfs(device, sysfs_root);
    probe_c2_feature(result, packet_transport);
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
