#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

enum class DevicePresence { unknown, absent, present };
enum class DeviceChangeReason { pending, initial, state_changed, signature_changed, reader_failure };
inline const char* device_presence_name(DevicePresence state) {
    switch (state) {
    case DevicePresence::unknown: return "UNKNOWN";
    case DevicePresence::absent: return "ABSENT";
    case DevicePresence::present: return "PRESENT";
    }
    return "UNKNOWN";
}
inline const char* device_change_reason_name(DeviceChangeReason reason) {
    switch (reason) {
    case DeviceChangeReason::pending: return "PENDING";
    case DeviceChangeReason::initial: return "INITIAL";
    case DeviceChangeReason::state_changed: return "STATE_CHANGED";
    case DeviceChangeReason::signature_changed: return "SIGNATURE_CHANGED";
    case DeviceChangeReason::reader_failure: return "READER_FAILURE";
    }
    return "PENDING";
}
struct DeviceObservation {
    DevicePresence presence = DevicePresence::unknown;
    std::string signature; // Internal OS observation, never a physical identity.
    std::string error;
};
struct DeviceObservationSnapshot {
    std::uint64_t generation = 0;
    DevicePresence presence = DevicePresence::unknown;
    DeviceChangeReason reason = DeviceChangeReason::pending;
    std::string error;
    bool sampled = false;
};
struct DeviceNodeInfo {
    DevicePresence presence = DevicePresence::unknown;
    std::uint64_t major = 0, minor = 0, inode = 0;
};
using DeviceNodeProbe = std::function<DeviceNodeInfo(const std::string&)>;
DeviceObservation observe_device_node(const std::string& device,
    const std::filesystem::path& sysfs_device_root = "/sys/dev/block", const DeviceNodeProbe& node_probe = {});

class DeviceObservationTracker {
public:
    const DeviceObservationSnapshot& observe(DeviceObservation value);
    const DeviceObservationSnapshot& reader_failed();
    const DeviceObservationSnapshot& snapshot() const { return snapshot_; }
private:
    DeviceObservationSnapshot snapshot_;
    std::string signature_;
};

// Only this owner probes node/sysfs metadata; main/reader methods copy a bounded
// cache. Sampling cannot detect a reset with no observable signature change.
class DeviceLifecycleObserver {
public:
    using Probe = std::function<DeviceObservation()>;
    explicit DeviceLifecycleObserver(Probe probe, std::chrono::milliseconds interval = std::chrono::milliseconds(250));
    ~DeviceLifecycleObserver();
    DeviceLifecycleObserver(const DeviceLifecycleObserver&) = delete;
    DeviceLifecycleObserver& operator=(const DeviceLifecycleObserver&) = delete;
    DeviceObservationSnapshot snapshot() const;
    bool permits(std::uint64_t generation) const;
    DeviceObservationSnapshot reader_failed();
private:
    void run();
    Probe probe_;
    std::chrono::milliseconds interval_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    DeviceObservationTracker tracker_;
    std::uint64_t mutation_ = 0;
    bool closing_ = false;
    std::thread thread_;
};

class CddaReader;
std::unique_ptr<CddaReader> make_device_epoch_reader(std::unique_ptr<CddaReader> reader,
                                                    std::function<bool()> current);
