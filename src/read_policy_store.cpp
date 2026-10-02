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

nlohmann::json encode(const ReadPolicy& policy) {
    return {{"schema_version", 1}, {"read_policy", {
        {"mode", policy.mode == ReadVerificationMode::repeat ? "repeat" : "single"},
        {"region_frames", policy.region_frames},
        {"required_matches", policy.required_matches},
        {"maximum_attempts", policy.maximum_attempts},
        {"time_budget_ms", policy.time_budget_ms}}}};
}

ReadPolicy decode(const nlohmann::json& root, std::size_t capacity) {
    if (!root.is_object() || root.size() != 2 || !root.contains("schema_version") ||
        root["schema_version"] != 1 || !root.contains("read_policy"))
        throw std::invalid_argument("unsupported settings schema");
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

std::string os_error() { return std::strerror(errno); }
}

std::optional<ReadPolicy> load_saved_read_policy(const std::string& path,
                                                  std::size_t capacity, std::string& error) {
    error.clear();
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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
    try { return decode(nlohmann::json::parse(bytes), capacity); }
    catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
}

bool save_read_policy(const std::string& path, const ReadPolicy& policy,
                      std::size_t capacity, std::string& error) {
    error.clear();
    try { validate_read_policy(policy, capacity); }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    const auto bytes = encode(policy).dump();
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
        // The rename has already made the new value visible. Report degraded
        // crash durability without telling the caller that nothing changed.
        error = "settings directory sync failed: " + os_error();
    }
    close(dirfd);
    return okay;
}
