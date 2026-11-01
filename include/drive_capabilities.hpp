#pragma once
#include "device_lifecycle.hpp"

#include <cstdint>
#include <cstddef>
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
    CapabilityFlag read_cache; // Reported current caching policy, not physical-cache absence/independence.
    CapabilityFlag accurate_stream;
    CapabilityFlag speed_control;
    // A successfully submitted CDROM_SELECT_SPEED request, not a measurement.
    std::optional<int> requested_speed_x;
    std::string speed_request_error;
    // Legacy multiples; never synthesized from a requested or maximum speed.
    std::optional<int> current_speed_x;
    std::optional<int> current_read_speed_kbytes_per_second;
    CapabilityEvidenceSource current_speed_source = CapabilityEvidenceSource::none;
    std::string current_speed_detail;
    std::optional<int> read_offset_samples;
    std::string probe_error;
    std::optional<DeviceObservationSnapshot> observation{};
    bool observation_revalidating = false;
    bool observation_retiring_io = false;
};

inline DriveCapabilities invalidated_drive_capabilities(const std::string& device,
    std::optional<int> configured_offset, const DeviceObservationSnapshot& observation) {
    DriveCapabilities result;
    result.device = device;
    result.read_offset_samples = configured_offset;
    result.observation = observation;
    return result;
}

inline bool bind_drive_probe(DriveCapabilities& current, DriveCapabilities probed,
                             std::uint64_t generation, const DeviceObservationSnapshot& observation) {
    if (generation != observation.generation) return false;
    const auto device = current.device;
    const auto offset = current.read_offset_samples;
    if (!observation.sampled || observation.presence != DevicePresence::present || probed.device != device) {
        current = invalidated_drive_capabilities(device, offset, observation);
        current.probe_error = "probe is not bound to a current device observation";
        return false;
    }
    current = std::move(probed);
    current.read_offset_samples = offset;
    current.observation = observation;
    return true;
}

// Test seam for a read-only MMC packet. Return zero on success or errno.
// テスト用の読み取り専用MMC packet境界。成功時は0、失敗時はerrnoを返す。
using DrivePacketTransport = std::function<int(std::span<const std::uint8_t>,
                                                std::span<std::uint8_t>,
                                                std::span<std::uint8_t>)>;

// Actual transfer length is required for MODE SENSE and READ CD. Unreturned
// zero-filled bytes must not become negative reports or clean C2 evidence.
using DriveReadPacketTransport = std::function<int(std::span<const std::uint8_t>,
    std::span<std::uint8_t>, std::span<std::uint8_t>, std::size_t& transferred)>;

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
DriveCapabilities probe_drive_capabilities(
    const std::string& device, const std::filesystem::path& sysfs_root,
    const DrivePacketTransport& packet_transport, const DriveReadPacketTransport& mode_transport);

// Requests a bounded CD speed multiple. A success means only that the ioctl
// accepted the request; it is not a measurement of the applied drive speed.
DriveSpeedRequestResult request_drive_speed(unsigned speed_x,
                                            const DriveSpeedTransport& transport);
DriveSpeedRequestResult request_drive_speed(const std::string& device, unsigned speed_x);

const char* knowledge_name(Knowledge value);
const char* capability_evidence_source_name(CapabilityEvidenceSource value);
