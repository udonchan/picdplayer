#pragma once

#include <filesystem>
#include <optional>
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
    std::optional<int> current_speed_x;
    std::optional<int> read_offset_samples;
    std::string probe_error;
};

// Best-effort, read-only probe. Failure leaves capabilities UNKNOWN and records
// probe_error; it does not make CD playback unavailable.
DriveCapabilities probe_drive_capabilities(
    const std::string& device,
    const std::filesystem::path& sysfs_root = "/sys/class/block");

const char* knowledge_name(Knowledge value);
const char* capability_evidence_source_name(CapabilityEvidenceSource value);
