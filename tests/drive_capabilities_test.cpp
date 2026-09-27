#include "drive_capabilities.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

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
} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / "picdplayer-drive-capability-test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "fake" / "device");
        { std::ofstream(root / "fake" / "device" / "vendor") << " VENDOR \n";
          std::ofstream(root / "fake" / "device" / "model") << "MODEL\n";
          std::ofstream(root / "fake" / "device" / "rev") << "1.0\n"; }

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

        std::filesystem::remove_all(root);
        std::cout << "PASS: capability probe preserves UNKNOWN and parses C2 Flags\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
