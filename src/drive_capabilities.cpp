#include "drive_capabilities.hpp"
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <linux/cdrom.h>
#include <sstream>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace {
std::string read_optional(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value)) return {};
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
}

DriveCapabilities probe_drive_capabilities(const std::string& device,
                                            const std::filesystem::path& sysfs_root) {
    DriveCapabilities result;
    result.device = device;
    const auto name = std::filesystem::path(device).filename();
    const auto base = sysfs_root / name / "device";
    result.vendor = read_optional(base / "vendor");
    result.model = read_optional(base / "model");
    result.firmware = read_optional(base / "rev");

    const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        result.probe_error = std::system_error(errno, std::generic_category(),
                                               "open " + device).what();
        return result;
    }
    const int capabilities = ioctl(fd, CDROM_GET_CAPABILITY, 0);
    const int error = errno;
    close(fd);
    if (capabilities < 0) {
        result.probe_error = std::system_error(error, std::generic_category(),
                                               "CDROM_GET_CAPABILITY " + device).what();
        return result;
    }
    result.speed_control.value = capabilities & CDC_SELECT_SPEED ? Knowledge::yes : Knowledge::no;
    result.speed_control.source = CapabilityEvidenceSource::kernel_reported;
    result.speed_control.detail = "CDROM_GET_CAPABILITY CDC_SELECT_SPEED";
    // CDC_PLAY_AUDIO concerns the drive's analogue/play command interface and
    // must not be presented as proof of digital audio extraction support.
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
