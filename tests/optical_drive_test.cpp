#include "optical_drive.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void device(const fs::path& root, const char* name, const char* type) {
    const auto path = root / name / "device";
    fs::create_directories(path);
    std::ofstream(path / "type") << type << '\n';
    std::ofstream(path / "vendor") << "ASUS    \n";
    std::ofstream(path / "model") << "SDRW-08D2S-U    \n";
}
int main() {
    char pattern[] = "/tmp/picdplayer-optical-XXXXXX";
    const char* directory = mkdtemp(pattern);
    if (!directory) return 1;
    const fs::path root(directory);
    try {
        require(find_optical_drives(root).empty(), "empty snapshot");
        device(root, "sda", "0");
        fs::create_directories(root / "mmcblk0");
        require(find_optical_drives(root).empty(), "exclude disks and SD cards");
        device(root, "sr1", "5");
        device(root, "sr0", "5");
        const auto drives = find_optical_drives(root);
        require(drives.size() == 2, "multiple drives");
        require(drives[0].device == "/dev/sr0" && drives[1].device == "/dev/sr1", "stable order");
        require(drives[0].vendor == "ASUS" && drives[0].model == "SDRW-08D2S-U", "trim attributes");
        fs::remove_all(root / "sr0");
        require(find_optical_drives(root).size() == 1, "fresh snapshot after removal");
        fs::remove(root / "sr1/device/model");
        bool failed = false;
        try { (void)find_optical_drives(root); }
        catch (const std::runtime_error&) { failed = true; }
        require(failed, "incomplete snapshot must fail explicitly");
        fs::remove_all(root);
        std::cout << "PASS: optical drive snapshots\n";
    } catch (const std::exception& error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
