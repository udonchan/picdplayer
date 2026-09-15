#include "pcm_file.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

void save_pcm_s16le(const std::string& path, std::span<const std::int16_t> pcm) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "create PCM " + path);
    bool closed = false;
    try {
        std::array<unsigned char, 4096> bytes{};
        std::size_t offset = 0;
        while (offset < pcm.size()) {
            std::size_t size = 0;
            while (offset < pcm.size() && size < bytes.size()) {
                const auto sample = static_cast<std::uint16_t>(pcm[offset++]);
                bytes[size++] = static_cast<unsigned char>(sample & 0xff);
                bytes[size++] = static_cast<unsigned char>(sample >> 8);
            }
            std::size_t written = 0;
            while (written < size) {
                const auto n = write(fd, bytes.data() + written, size - written);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) throw std::system_error(n < 0 ? errno : EIO, std::generic_category(), "write PCM " + path);
                written += static_cast<std::size_t>(n);
            }
        }
        const int result = close(fd);
        closed = true;
        if (result < 0) throw std::system_error(errno, std::generic_category(), "close PCM " + path);
    } catch (...) {
        if (!closed) close(fd);
        unlink(path.c_str());
        throw;
    }
}
