#pragma once
#include <filesystem>
#include <string>
#include <vector>

struct OpticalDrive {
    std::filesystem::path device;
    std::string vendor;
    std::string model;
};

// Snapshot of kernel-recognized SCSI CD/DVD drives; does not inspect media.
// sysfs_root is injectable for hardware-independent tests.
std::vector<OpticalDrive> find_optical_drives(
    const std::filesystem::path& sysfs_root = "/sys/class/block");
