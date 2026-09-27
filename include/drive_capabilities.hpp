#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>

enum class Knowledge { unknown, no, yes };
enum class CapabilityEvidenceSource { none, kernel_reported, drive_reported, tested, database, user_configured, inferred };

struct CapabilityFlag {
    Knowledge value = Knowledge::unknown;
    CapabilityEvidenceSource source = CapabilityEvidenceSource::none;
    std::string detail;
};

struct DriveCapabilities {
    std::string device;
    std::string vendor;
    std::string model;
    std::string firmware;
    CapabilityFlag digital_audio_extraction;
    CapabilityFlag c2_supported;
    CapabilityFlag c2_trustworthy;
    CapabilityFlag read_cache;
    CapabilityFlag accurate_stream;
    CapabilityFlag speed_control;
    // A successfully submitted CDROM_SELECT_SPEED request, not a measurement.
    std::optional<int> requested_speed_x;
    std::string speed_request_error;
    // A measured/reported current speed. No current-speed query exists yet.
    std::optional<int> current_speed_x;
    std::optional<int> read_offset_samples;
    std::string probe_error;
};

// Test seam for a read-only MMC packet. Return zero on success or errno.
// テスト用の読み取り専用MMC packet境界。成功時は0、失敗時はerrnoを返す。
using DrivePacketTransport = std::function<int(std::span<const std::uint8_t>,
                                                std::span<std::uint8_t>,
                                                std::span<std::uint8_t>)>;

// Test seam for CDROM_SELECT_SPEED. The argument is a CD speed multiple and
// the return value is zero on success or errno.
// テスト用のCDROM_SELECT_SPEED境界。引数はCD倍速、成功時は0、失敗時はerrnoを返す。
using DriveSpeedTransport = std::function<int(unsigned)>;

struct DriveSpeedRequestResult {
    std::optional<int> requested_speed_x;
    std::string error;
};

// Best-effort, read-only probe. Failure leaves capabilities UNKNOWN and records
// probe_error; it does not make CD playback unavailable.
DriveCapabilities probe_drive_capabilities(
    const std::string& device,
    const std::filesystem::path& sysfs_root = "/sys/class/block");
// This overload is a test seam for the MMC C2 Feature probe. It intentionally
// does not open the device or invoke CDROM_GET_CAPABILITY.
DriveCapabilities probe_drive_capabilities(
    const std::string& device, const std::filesystem::path& sysfs_root,
    const DrivePacketTransport& packet_transport);

// Requests a bounded CD speed multiple. A success means only that the ioctl
// accepted the request; it is not a measurement of the applied drive speed.
DriveSpeedRequestResult request_drive_speed(unsigned speed_x,
                                            const DriveSpeedTransport& transport);
DriveSpeedRequestResult request_drive_speed(const std::string& device, unsigned speed_x);

const char* knowledge_name(Knowledge value);
const char* capability_evidence_source_name(CapabilityEvidenceSource value);
