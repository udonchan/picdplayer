#include "drive_capabilities.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace { void check(bool value) { if (!value) throw std::runtime_error("drive capability test failed"); } }

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / "picdplayer-drive-capability-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "fake" / "device");
        { std::ofstream(root / "fake" / "device" / "vendor") << " VENDOR \n";
          std::ofstream(root / "fake" / "device" / "model") << "MODEL\n";
          std::ofstream(root / "fake" / "device" / "rev") << "1.0\n"; }
        const auto result = probe_drive_capabilities("/nonexistent/fake", root);
        check(result.vendor == "VENDOR" && result.model == "MODEL" && result.firmware == "1.0");
        check(result.digital_audio_extraction.value == Knowledge::unknown);
        check(result.c2_supported.value == Knowledge::unknown);
        check(result.speed_control.value == Knowledge::unknown);
        check(!result.probe_error.empty());
        std::filesystem::remove_all(root);
        std::cout << "PASS: capability UNKNOWN and best-effort identity probe\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
