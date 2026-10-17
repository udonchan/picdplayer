#include "device_lifecycle.hpp"
#include "cdda_reader.hpp"
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <cerrno>
#ifdef __linux__
#include <sys/stat.h>
#include <sys/sysmacros.h>
#endif

namespace {
DeviceNodeInfo native_node(const std::string& path) {
#ifdef __linux__
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0)
        return {errno == ENOENT || errno == ENXIO ? DevicePresence::absent : DevicePresence::unknown};
    if (!S_ISBLK(info.st_mode)) return {};
    return {DevicePresence::present, static_cast<std::uint64_t>(major(info.st_rdev)),
            static_cast<std::uint64_t>(minor(info.st_rdev)), static_cast<std::uint64_t>(info.st_ino)};
#else
    (void)path;
    return {};
#endif
}
std::string number_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("metadata unavailable");
    char bytes[129]{};
    stream.read(bytes, sizeof(bytes));
    if (stream.bad() || stream.gcount() == static_cast<std::streamsize>(sizeof(bytes))) throw std::runtime_error("metadata limit");
    std::string_view text(bytes, static_cast<std::size_t>(stream.gcount()));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) throw std::runtime_error("metadata format");
    return std::to_string(number);
}

class EpochReader final : public CddaReader {
    std::unique_ptr<CddaReader> reader_;
    std::function<bool()> current_;
    void check() const {
        if (!current_()) throw std::system_error(ESTALE, std::generic_category(), "device observation changed");
    }
public:
    EpochReader(std::unique_ptr<CddaReader> reader, std::function<bool()> current)
        : reader_(std::move(reader)), current_(std::move(current)) {
        if (!reader_ || !current_) throw std::invalid_argument("empty device epoch reader");
    }
    void seek(std::int32_t position) override { check(); reader_->seek(position); check(); }
    ReadResult read(std::span<std::int16_t> pcm) override {
        check(); const auto result = reader_->read(pcm); check(); return result;
    }
};
}

DeviceObservation observe_device_node(const std::string& device, const std::filesystem::path& root,
                                      const DeviceNodeProbe& probe) {
    try {
        const auto node = probe ? probe(device) : native_node(device);
        if (node.presence == DevicePresence::absent) return {DevicePresence::absent, {}, {}};
        if (node.presence != DevicePresence::present) return {DevicePresence::unknown, {}, "NODE_STATUS_UNAVAILABLE"};
        const auto target = std::filesystem::canonical(root / (std::to_string(node.major) + ":" + std::to_string(node.minor)));
        auto signature = std::to_string(node.major) + ":" + std::to_string(node.minor) + ":" +
                         std::to_string(node.inode) + ":" + target.string();
        auto ancestor = target;
        for (;;) {
            const bool bus = std::filesystem::exists(ancestor / "busnum");
            const bool dev = std::filesystem::exists(ancestor / "devnum");
            if (bus != dev) return {DevicePresence::unknown, {}, "USB_METADATA_INCOMPLETE"};
            if (bus) {
                signature += ":" + number_file(ancestor / "busnum") + ":" + number_file(ancestor / "devnum");
                break;
            }
            if (ancestor == ancestor.root_path()) break;
            ancestor = ancestor.parent_path();
        }
        if (signature.size() > 4096) return {DevicePresence::unknown, {}, "METADATA_LIMIT"};
        return {DevicePresence::present, std::move(signature), {}};
    } catch (...) { return {DevicePresence::unknown, {}, "METADATA_UNAVAILABLE"}; }
}

const DeviceObservationSnapshot& DeviceObservationTracker::observe(DeviceObservation value) {
    if ((value.presence != DevicePresence::unknown && value.presence != DevicePresence::absent && value.presence != DevicePresence::present) ||
        value.signature.size() > 4096 || value.error.size() > 256 ||
        (value.presence == DevicePresence::present && value.signature.empty()))
        value = {DevicePresence::unknown, {}, "METADATA_LIMIT"};
    const bool first = !snapshot_.sampled;
    const bool state_changed = snapshot_.presence != value.presence;
    const bool signature_changed = value.presence == DevicePresence::present && signature_ != value.signature;
    if (first || state_changed || signature_changed) {
        ++snapshot_.generation;
        snapshot_.reason = first ? DeviceChangeReason::initial : state_changed
            ? DeviceChangeReason::state_changed : DeviceChangeReason::signature_changed;
    }
    snapshot_.sampled = true;
    snapshot_.presence = value.presence;
    snapshot_.error = std::move(value.error);
    signature_ = std::move(value.signature);
    return snapshot_;
}
const DeviceObservationSnapshot& DeviceObservationTracker::reader_failed() {
    ++snapshot_.generation;
    snapshot_.reason = DeviceChangeReason::reader_failure;
    return snapshot_;
}

DeviceLifecycleObserver::DeviceLifecycleObserver(Probe probe, std::chrono::milliseconds interval)
    : probe_(std::move(probe)), interval_(interval) {
    if (!probe_ || interval_.count() <= 0) throw std::invalid_argument("invalid device observer");
    thread_ = std::thread(&DeviceLifecycleObserver::run, this);
}
DeviceLifecycleObserver::~DeviceLifecycleObserver() {
    { std::lock_guard lock(mutex_); closing_ = true; }
    changed_.notify_all();
    thread_.join();
}
DeviceObservationSnapshot DeviceLifecycleObserver::snapshot() const {
    std::lock_guard lock(mutex_); return tracker_.snapshot();
}
bool DeviceLifecycleObserver::permits(std::uint64_t generation) const {
    std::lock_guard lock(mutex_);
    return tracker_.snapshot().generation == generation && tracker_.snapshot().presence != DevicePresence::absent;
}
DeviceObservationSnapshot DeviceLifecycleObserver::reader_failed() {
    std::lock_guard lock(mutex_);
    ++mutation_;
    return tracker_.reader_failed();
}
void DeviceLifecycleObserver::run() {
    for (;;) {
        std::uint64_t mutation;
        { std::lock_guard lock(mutex_); if (closing_) return; mutation = mutation_; }
        DeviceObservation value;
        try { value = probe_(); }
        catch (...) { value = {DevicePresence::unknown, {}, "OBSERVATION_FAILED"}; }
        std::unique_lock lock(mutex_);
        if (closing_) return;
        if (mutation == mutation_) tracker_.observe(std::move(value));
        changed_.wait_for(lock, interval_, [&] {return closing_;});
        if (closing_) return;
    }
}

std::unique_ptr<CddaReader> make_device_epoch_reader(std::unique_ptr<CddaReader> reader, std::function<bool()> current) {
    return std::make_unique<EpochReader>(std::move(reader), std::move(current));
}
