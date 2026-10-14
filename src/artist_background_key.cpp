#include "artist_background_key.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
class FileDescriptor {
public:
    explicit FileDescriptor(int descriptor) : descriptor_(descriptor) {}
    ~FileDescriptor() { if (descriptor_ >= 0) ::close(descriptor_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return descriptor_; }
private:
    int descriptor_;
};
}

std::string resolve_artist_background_key(const std::string& path) {
    if (!path.empty()) return load_artist_background_key(path);
    const char* value = std::getenv("FANART_TV_API_KEY");
    if (!value || !*value) return {};
    std::string contents;
    for (std::size_t i = 0; value[i]; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (i >= 256 || c <= 0x20 || c >= 0x7f)
            throw std::runtime_error("artist background key has invalid contents");
        contents.push_back(value[i]);
    }
    return contents;
}

std::string load_artist_background_key(const std::string& path) {
    if (path.empty()) throw std::invalid_argument("artist background key path is empty");
    FileDescriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (file.get() < 0) throw std::runtime_error("artist background key file could not be opened");
    struct stat info{};
    if (::fstat(file.get(), &info) != 0 || !S_ISREG(info.st_mode) || (info.st_mode & 0027) != 0 ||
        info.st_size <= 0 || info.st_size > 257)
        throw std::runtime_error("artist background key file is not a protected regular file");
    std::string contents;
    char buffer[258];
    for (;;) {
        const auto count = ::read(file.get(), buffer, sizeof(buffer));
        if (count < 0) throw std::runtime_error("artist background key file could not be read");
        if (count == 0) break;
        contents.append(buffer, static_cast<std::size_t>(count));
        if (contents.size() > 257) throw std::runtime_error("artist background key file is too large");
    }
    if (!contents.empty() && contents.back() == '\n') contents.pop_back();
    if (contents.empty() || contents.size() > 256 ||
        !std::all_of(contents.begin(), contents.end(), [](unsigned char c) { return c > 0x20 && c < 0x7f; }))
        throw std::runtime_error("artist background key file has invalid contents");
    return contents;
}
