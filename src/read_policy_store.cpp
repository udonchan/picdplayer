#include "read_policy_store.hpp"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr std::size_t maximum_settings_bytes = 4096;

ReadPolicy decode_read_policy(const nlohmann::json& root, std::size_t capacity) {
    const auto& value = root["read_policy"];
    if (!value.is_object() || value.size() != 5 || !value.contains("mode") ||
        !value["mode"].is_string() || !value.contains("region_frames") ||
        !value.contains("required_matches") || !value.contains("maximum_attempts") ||
        !value.contains("time_budget_ms"))
        throw std::invalid_argument("invalid saved read policy");
    const auto number = [&](const char* key) -> unsigned {
        if (!value[key].is_number_unsigned()) throw std::invalid_argument("invalid saved policy number");
        const auto parsed = value[key].get<unsigned long long>();
        if (parsed > std::numeric_limits<unsigned>::max())
            throw std::invalid_argument("saved policy number too large");
        return static_cast<unsigned>(parsed);
    };
    ReadPolicy policy{parse_read_verification_mode(value["mode"].get<std::string>()),
                      number("region_frames"), number("required_matches"),
                      number("maximum_attempts"), number("time_budget_ms")};
    validate_read_policy(policy, capacity);
    return policy;
}

nlohmann::json encode_settings(const SettingsSnapshot& snapshot) {
    return {{"schema_version", 2}, {"read_policy", {
        {"mode", snapshot.read_policy.mode == ReadVerificationMode::repeat ? "repeat" : "single"},
        {"region_frames", snapshot.read_policy.region_frames},
        {"required_matches", snapshot.read_policy.required_matches},
        {"maximum_attempts", snapshot.read_policy.maximum_attempts},
        {"time_budget_ms", snapshot.read_policy.time_budget_ms}}},
        {"artist_background_enabled", snapshot.artist_background_enabled}};
}

SettingsSnapshot decode_settings(const nlohmann::json& root, std::size_t capacity) {
    if (!root.is_object() || !root.contains("schema_version") || !root["schema_version"].is_number_integer() ||
        !root.contains("read_policy"))
        throw std::invalid_argument("unsupported settings schema");
    const auto version = root["schema_version"];
    SettingsSnapshot snap;
    if (version == 2) {
        if (root.size() != 3 || !root.contains("artist_background_enabled") ||
            !root["artist_background_enabled"].is_boolean())
            throw std::invalid_argument("missing or invalid artist_background_enabled");
    } else if (version == 1) {
        if (root.size() != 2)
            throw std::invalid_argument("unsupported settings schema");
        snap.artist_background_enabled = false;
    } else {
        throw std::invalid_argument("unknown settings schema version");
    }
    snap.read_policy = decode_read_policy(root, capacity);
    if (version == 2) snap.artist_background_enabled = root["artist_background_enabled"].get<bool>();
    return snap;
}

std::string os_error() { return std::strerror(errno); }
}

std::optional<SettingsSnapshot> load_settings(const std::string& path,
                                              std::size_t capacity, std::string& error) {
    error.clear();
    // Reject special files after open without waiting for a FIFO writer.
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) error = os_error();
        return std::nullopt;
    }
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_size > static_cast<off_t>(maximum_settings_bytes)) {
        error = "settings file is not a bounded regular file";
        close(fd);
        return std::nullopt;
    }
    std::string bytes;
    char buffer[1024];
    ssize_t count = 0;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0 && bytes.size() <= maximum_settings_bytes)
        bytes.append(buffer, static_cast<std::size_t>(count));
    const int saved_errno = errno;
    close(fd);
    if (count < 0) { error = std::strerror(saved_errno); return std::nullopt; }
    if (bytes.size() > maximum_settings_bytes) { error = "settings file too large"; return std::nullopt; }
    try {
        return decode_settings(nlohmann::json::parse(bytes), capacity);
    } catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
}

bool save_settings(const std::string& path, const SettingsSnapshot& snapshot,
                   std::size_t capacity, std::string& error) {
    error.clear();
    try { validate_read_policy(snapshot.read_policy, capacity); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    const auto bytes = encode_settings(snapshot).dump();
    const auto directory = fs::path(path).parent_path();
    if (directory.empty() || bytes.size() > maximum_settings_bytes) {
        error = "invalid settings path or size";
        return false;
    }
    const int dirfd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) { error = os_error(); return false; }
    const auto filename = fs::path(path).filename().string();
    if (filename.empty() || filename == "." || filename == "..") {
        error = "invalid settings filename"; close(dirfd); return false;
    }
    std::string temporary = filename + ".tmp.XXXXXX";
    std::string template_path = (directory / temporary).string();
    std::vector<char> name(template_path.begin(), template_path.end());
    name.push_back('\0');
    const int fd = mkostemp(name.data(), O_CLOEXEC);
    if (fd < 0) { error = os_error(); close(dirfd); return false; }
    bool okay = fchmod(fd, 0600) == 0;
    std::size_t written = 0;
    while (okay && written < bytes.size()) {
        const auto count = write(fd, bytes.data() + written, bytes.size() - written);
        if (count <= 0) okay = false;
        else written += static_cast<std::size_t>(count);
    }
    if (okay) okay = fsync(fd) == 0;
    if (close(fd) != 0) okay = false;
    if (okay) okay = rename(name.data(), path.c_str()) == 0;
    if (!okay) { error = os_error(); unlink(name.data()); }
    else if (fsync(dirfd) != 0) {
        error = "settings directory sync failed: " + os_error();
    }
    close(dirfd);
    return okay;
}
