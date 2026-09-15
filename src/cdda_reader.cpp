#include "cdda_reader.hpp"
#include "linux_ioctl_reader.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <limits>
#include <linux/cdrom.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
#include <utility>

CddaBackend parse_cdda_backend(std::string_view name) {
    if (name == "direct") return CddaBackend::direct;
    if (name == "paranoia") return CddaBackend::paranoia;
    throw std::invalid_argument("unknown CDDA backend: " + std::string(name));
}
void require_cdda_backend(CddaBackend backend) {
    if (backend == CddaBackend::paranoia)
        throw std::invalid_argument("paranoia backend is not built (ENABLE_PARANOIA=OFF)");
    if (backend != CddaBackend::direct) throw std::invalid_argument("invalid CDDA backend");
}
LinuxIoctlReader::LinuxIoctlReader(AudioRead transport, DirectOptions options)
    : transport_(std::move(transport)), options_(options) {
    if (!transport_ || options.retries > 10) throw std::invalid_argument("invalid direct reader options");
}
void LinuxIoctlReader::seek(std::int32_t lba) {
    if (lba < 0) throw std::invalid_argument("negative CDDA LBA");
    cursor_ = lba;
    positioned_ = true;
}
ReadResult LinuxIoctlReader::read(std::span<std::int16_t> pcm) {
    if (!positioned_) throw std::logic_error("CDDA seek required before read");
    if (pcm.empty() || pcm.size() % cdda_samples_per_frame)
        throw std::invalid_argument("PCM buffer must contain whole CD frames");
    const auto count = pcm.size() / cdda_samples_per_frame;
    if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - cursor_))
        throw std::invalid_argument("CDDA LBA overflow");
    ReadResult result{cursor_, count, 0, ReadStatus::ok, 0, 0};
    std::array<std::int16_t, 75 * cdda_samples_per_frame> scratch{};
    while (result.frames_read < count) {
        const auto frames = std::min<std::size_t>(75, count - result.frames_read);
        const auto block = std::span(scratch).first(frames * cdda_samples_per_frame);
        int error = 0;
        unsigned attempt = 0;
        do {
            error = transport_(cursor_, block);
            if (!error) break;
            // Only an I/O error gets optional retries; permissions, removal etc. fail immediately.
            if (error != EIO || attempt == options_.retries) break;
            ++attempt;
            ++result.retries;
        } while (true);
        if (error) {
            positioned_ = false;
            result.status = ReadStatus::read_error;
            result.native_error = error;
            return result;
        }
        std::copy(block.begin(), block.end(), pcm.begin() + result.frames_read * cdda_samples_per_frame);
        cursor_ += static_cast<std::int32_t>(frames);
        result.frames_read += frames;
    }
    return result;
}
namespace {
struct DeviceFd {
    int value;
    explicit DeviceFd(const std::string& device)
        : value(open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) {
        if (value < 0) throw std::system_error(errno, std::generic_category(), "open " + device);
    }
    ~DeviceFd() { close(value); }
    DeviceFd(const DeviceFd&) = delete;
    DeviceFd& operator=(const DeviceFd&) = delete;
};
}
std::unique_ptr<CddaReader> make_cdda_reader(CddaBackend backend,
    const std::string& device, DirectOptions options) {
    require_cdda_backend(backend);
    if (options.retries > 10) throw std::invalid_argument("direct retries must be 0..10");
    auto fd = std::make_shared<DeviceFd>(device);
    return std::make_unique<LinuxIoctlReader>([fd](std::int32_t lba, std::span<std::int16_t> pcm) {
        cdrom_read_audio request{};
        request.addr.lba = lba;
        request.addr_format = CDROM_LBA;
        request.nframes = static_cast<int>(pcm.size() / cdda_samples_per_frame);
        request.buf = reinterpret_cast<unsigned char*>(pcm.data());
        if (ioctl(fd->value, CDROMREADAUDIO, &request) < 0) return errno;
        // MMC little-endian audio assumption, to be verified on the actual drive.
        // Normalize bytes explicitly to the common host-endian PCM contract.
        if constexpr (std::endian::native == std::endian::big) {
            for (auto& sample : pcm) {
                const auto word = std::bit_cast<std::uint16_t>(sample);
                sample = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>((word >> 8) | (word << 8)));
            }
        }
        return 0;
    }, options);
}
