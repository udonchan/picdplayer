#include "optical_drive.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {
std::string read_attribute(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::string value;
    if (!std::getline(stream, value))
        throw std::runtime_error("cannot read sysfs attribute: " + path.string());
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
}

std::vector<OpticalDrive> find_optical_drives(const std::filesystem::path& sysfs_root) {
    std::vector<OpticalDrive> drives;
    for (const auto& entry : std::filesystem::directory_iterator(sysfs_root)) {
        const auto type_path = entry.path() / "device/type";
        // Non-SCSI block devices (e.g. SD cards) need not expose this attribute.
        if (!std::filesystem::exists(type_path)) continue;
        if (read_attribute(type_path) != "5") continue; // SCSI CD/DVD peripheral type
        drives.push_back({std::filesystem::path("/dev") / entry.path().filename(),
                          read_attribute(entry.path() / "device/vendor"),
                          read_attribute(entry.path() / "device/model")});
    }
    std::sort(drives.begin(), drives.end(), [](const auto& a, const auto& b) {
        return a.device < b.device;
    });
    return drives;
}
