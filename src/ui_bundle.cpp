#include "ui_bundle.hpp"
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <memory>
#include <stdexcept>

namespace {
constexpr std::size_t file_limit = 256 * 1024, total_limit = 2 * 1024 * 1024;
struct Fd {
    int value;
    ~Fd() { if (value >= 0) ::close(value); }
    explicit Fd(int value) : value(value) {}
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
bool safe_path(std::string_view path) {
    if (path.empty() || path.size() > 240) return false;
    while (!path.empty()) {
        auto n = path.find('/');
        auto part = path.substr(0, n);
        if (part.empty() || part == "." || part == "..") return false;
        for (unsigned char c : part)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
        if (n == path.npos) return true;
        path.remove_prefix(n + 1);
        if (path.empty()) return false;
    }
    return false;
}
std::string mime(std::string_view name) {
    const auto ext = name.substr(name.rfind('.') == name.npos ? name.size() : name.rfind('.'));
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".woff2") return "font/woff2";
    throw std::runtime_error("unsupported asset extension");
}
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t count = 0;
        unsigned codepoint = 0;
        unsigned minimum = 0;
        if (c <= 0x7f) { ++i; continue; }
        if (c >= 0xc2 && c <= 0xdf) { count = 1; codepoint = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; codepoint = c & 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; codepoint = c & 0x07; minimum = 0x10000; }
        else return false;
        if (i + count >= text.size()) return false;
        for (std::size_t j = 1; j <= count; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        i += count + 1;
    }
    return true;
}
void collect(int directory, const std::string& prefix,
             std::map<std::string, UiAsset, std::less<>>& assets,
             std::size_t& total, unsigned depth, unsigned& count) {
    if (depth > 8) throw std::runtime_error("UI directory depth exceeds 8");
    const int copy = dup(directory);
    if (copy < 0) throw std::runtime_error("cannot duplicate UI directory descriptor");
    DIR* raw = fdopendir(copy);
    if (!raw) { close(copy); throw std::runtime_error("cannot enumerate UI directory"); }
    const auto close_directory = [](DIR* handle) { (void)closedir(handle); };
    std::unique_ptr<DIR, decltype(close_directory)> dir(raw, close_directory);
    while (true) {
        errno = 0;
        auto* item = readdir(dir.get());
        if (!item) { if (errno) throw std::runtime_error("UI directory read failed"); break; }
        std::string name = item->d_name;
        if (name == "." || name == "..") continue;
        if (++count > 64) throw std::runtime_error("UI exceeds 64 entries");
        const auto path = prefix + name;
        if (!safe_path(path)) throw std::runtime_error("unsafe UI asset path");
        struct stat before{};
        if (fstatat(directory, name.c_str(), &before, AT_SYMLINK_NOFOLLOW) ||
            (!S_ISREG(before.st_mode) && !S_ISDIR(before.st_mode)))
            throw std::runtime_error("UI contains a symlink or special file");
        Fd fd(openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (fd.value < 0) throw std::runtime_error("cannot open UI asset (symlinks are forbidden)");
        struct stat st{};
        if (fstat(fd.value, &st)) throw std::runtime_error("cannot stat UI asset");
        if (S_ISDIR(st.st_mode)) { collect(fd.value, path + '/', assets, total, depth + 1, count); continue; }
        if (!S_ISREG(st.st_mode)) throw std::runtime_error("UI asset must be a regular file");
        if (st.st_size < 0 || st.st_size > static_cast<off_t>(file_limit)) throw std::runtime_error("UI asset exceeds 256 KiB");
        UiAsset asset{mime(name), {}};
        char buffer[8192];
        while (true) {
            auto n = read(fd.value, buffer, sizeof(buffer));
            if (n < 0) { if (errno == EINTR) continue; throw std::runtime_error("UI asset read failed"); }
            if (!n) break;
            if (asset.bytes.size() + n > file_limit || total + n > total_limit)
                throw std::runtime_error("UI size limit exceeded");
            asset.bytes.append(buffer, static_cast<std::size_t>(n)); total += n;
        }
        if (asset.mime.starts_with("text/") || asset.mime == "application/json") {
            if (!valid_utf8(asset.bytes)) throw std::runtime_error("text asset is not valid UTF-8");
        }
        assets.emplace(path, std::move(asset));
    }
}
// Separate semantic validation from bounded filesystem loading.
std::string validate(const std::map<std::string, UiAsset, std::less<>>& assets) {
    auto m = assets.find("manifest.json");
    if (m == assets.end()) throw std::runtime_error("manifest.json is missing");
    auto j = nlohmann::json::parse(m->second.bytes, [](int depth, auto, auto&) {
        if (depth > 16) throw std::runtime_error("manifest nesting exceeds 16");
        return true;
    });
    if (!j.is_object() || !j.contains("picdplayer_ui") || !j["picdplayer_ui"].is_number_integer() || j["picdplayer_ui"] != 1)
        throw std::runtime_error("unsupported or missing picdplayer_ui version");
    if (!j.contains("requires_api") || !j["requires_api"].is_number_integer() || j["requires_api"] != 1)
        throw std::runtime_error("unsupported or missing requires_api version");
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get_ref<const std::string&>().empty() ||
        j["name"].get_ref<const std::string&>().size() > 128) throw std::runtime_error("invalid manifest name");
    if (!j.contains("entry") || !j["entry"].is_string()) throw std::runtime_error("manifest entry is missing");
    auto entry = j["entry"].get<std::string>();
    if (!safe_path(entry) || !entry.ends_with(".html") || !assets.contains(entry) || assets.at(entry).bytes.empty())
        throw std::runtime_error("invalid or missing HTML entry");
    for (const auto* required : {"player.css", "player.js"})
        if (!assets.contains(required)) throw std::runtime_error("player.css or player.js is missing");
    return entry;
}
}
UiBundle UiBundle::load(const std::string& directory) {
    UiBundle result;
    if (directory.empty()) return result;
    try {
        // Open every directory component without following symlinks, including parents.
        Fd current(open(directory.front() == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        std::size_t pos = directory.front() == '/' ? 1 : 0;
        while (pos < directory.size()) {
            auto end = directory.find('/', pos);
            auto part = directory.substr(pos, end == directory.npos ? end : end - pos);
            if (part == "..") throw std::runtime_error("parent traversal in UI directory");
            if (!part.empty() && part != ".") {
                int next = openat(current.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                if (next < 0) throw std::runtime_error("UI directory missing, inaccessible or symlinked");
                close(current.value); current.value = next;
            }
            if (end == directory.npos) break;
            pos = end + 1;
        }
        std::size_t total = 0; unsigned count = 0;
        collect(current.value, "", result.assets_, total, 0, count);
        result.entry_ = validate(result.assets_);
    } catch (const nlohmann::json::exception&) {
        result.assets_.clear(); result.error_ = "malformed manifest, field types or text encoding";
    } catch (const std::exception& e) {
        result.assets_.clear(); result.error_ = e.what();
    }
    return result;
}
const UiAsset* UiBundle::find(std::string_view path) const {
    if (!custom()) return nullptr;
    if (path == "/player" || path == "/player/") path = entry_;
    else if (path == "/player.css") path = "player.css";
    else if (path == "/player.js") path = "player.js";
    else if (path.starts_with("/player/")) path.remove_prefix(8);
    else return nullptr;
    if (!safe_path(path)) return nullptr;
    const auto it = assets_.find(path);
    return it == assets_.end() ? nullptr : &it->second;
}
