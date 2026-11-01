#include "cdda_reader.hpp"
#include "linux_ioctl_reader.hpp"
#include "mmc_read_transport.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <climits>
#include <fcntl.h>
#include <limits>
#include <linux/cdrom.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
#include <utility>

#ifdef ENABLE_PARANOIA
std::unique_ptr<CddaReader> make_paranoia_reader(const std::string& device);
#endif

namespace {
constexpr std::uint8_t read_cd = 0xbe;
constexpr std::size_t read_cd_cdb_bytes = 12;
constexpr std::size_t cdda_bytes_per_frame = cdda_samples_per_frame * sizeof(std::int16_t);
constexpr std::size_t c2_pointer_bytes_per_frame = 294;
void put_be24(std::uint8_t* destination, std::size_t value) {
    destination[0] = static_cast<std::uint8_t>(value >> 16);
    destination[1] = static_cast<std::uint8_t>(value >> 8);
    destination[2] = static_cast<std::uint8_t>(value);
}
void put_be32(std::uint8_t* destination, std::int32_t value) {
    const auto encoded = static_cast<std::uint32_t>(value);
    destination[0] = static_cast<std::uint8_t>(encoded >> 24);
    destination[1] = static_cast<std::uint8_t>(encoded >> 16);
    destination[2] = static_cast<std::uint8_t>(encoded >> 8);
    destination[3] = static_cast<std::uint8_t>(encoded);
}
}

C2AudioRead make_mmc_c2_audio_read(DriveReadPacketTransport transport) {
    if (!transport) throw std::invalid_argument("C2 packet transport is required");
    return [transport = std::move(transport)](std::int32_t lba, std::span<std::int16_t> pcm) {
        if (lba < 0 || pcm.empty() || pcm.size() % cdda_samples_per_frame)
            return C2AudioReadResult{EINVAL, C2Status::unknown};
        const auto frames = pcm.size() / cdda_samples_per_frame;
        if (frames > 75 || frames > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - lba))
            return C2AudioReadResult{EINVAL, C2Status::unknown};
        std::array<std::uint8_t, read_cd_cdb_bytes> command{};
        command[0] = read_cd;
        command[1] = 0x04; // expected sector type: CD-DA
        put_be32(command.data() + 2, lba);
        put_be24(command.data() + 6, frames);
        command[9] = 0x12; // user data plus C2 error pointers
        std::vector<std::uint8_t> response(frames * (cdda_bytes_per_frame + c2_pointer_bytes_per_frame));
        std::array<std::uint8_t, sizeof(request_sense)> sense{};
        std::size_t transferred = 0;
        const auto error = transport(command, response, sense, transferred);
        if (error) return C2AudioReadResult{error, C2Status::unknown};
        if (transferred != response.size()) return C2AudioReadResult{EMSGSIZE, C2Status::unknown};
        bool reported = false;
        // Same little-endian MMC audio assumption as the ordinary path; the
        // host contract is normalized, not evidence of drive byte-order tests.
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const auto offset = frame * (cdda_bytes_per_frame + c2_pointer_bytes_per_frame);
            for (std::size_t sample = 0; sample < cdda_samples_per_frame; ++sample) {
                const auto low = response[offset + 2 * sample];
                const auto high = response[offset + 2 * sample + 1];
                pcm[frame * cdda_samples_per_frame + sample] =
                    std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(low | (static_cast<unsigned>(high) << 8)));
            }
            const auto c2 = std::span(response).subspan(offset + cdda_bytes_per_frame,
                                                         c2_pointer_bytes_per_frame);
            reported = reported || std::any_of(c2.begin(), c2.end(), [](std::uint8_t value) { return value != 0; });
        }
        return C2AudioReadResult{0, reported ? C2Status::reported : C2Status::clean};
    };
}

CddaBackend parse_cdda_backend(std::string_view name) {
    if (name == "direct") return CddaBackend::direct;
    if (name == "paranoia") return CddaBackend::paranoia;
    throw std::invalid_argument("unknown CDDA backend: " + std::string(name));
}
void require_cdda_backend(CddaBackend backend) {
    if (backend == CddaBackend::paranoia) {
#ifdef ENABLE_PARANOIA
        return;
#else
        throw std::invalid_argument("paranoia backend is not built (ENABLE_PARANOIA=OFF)");
#endif
    }
    if (backend != CddaBackend::direct) throw std::invalid_argument("invalid CDDA backend");
}
LinuxIoctlReader::LinuxIoctlReader(AudioRead transport, DirectOptions options,
                                   C2AudioRead c2_transport)
    : transport_(std::move(transport)), options_(options), c2_transport_(std::move(c2_transport)) {
    if (!transport_ || options.retries > 10 || options.c2_timeout_ms == 0 || options.c2_timeout_ms > 5000 ||
        (options.require_c2_pointers && !options.request_c2_pointers) ||
        (options.request_c2_pointers && !c2_transport_) ||
        (!options.request_c2_pointers && options.inactive_c2_status != C2Status::not_checked &&
         options.inactive_c2_status != C2Status::not_available))
        throw std::invalid_argument("invalid direct reader options");
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
    result.c2_status = options_.request_c2_pointers ? C2Status::unknown : options_.inactive_c2_status;
    std::array<std::int16_t, 75 * cdda_samples_per_frame> scratch{};
    while (result.frames_read < count) {
        const auto frames = std::min<std::size_t>(75, count - result.frames_read);
        const auto block = std::span(scratch).first(frames * cdda_samples_per_frame);
        int error = 0;
        C2Status block_c2 = options_.request_c2_pointers
            ? C2Status::not_checked : options_.inactive_c2_status;
        if (options_.request_c2_pointers) {
            const auto c2_result = c2_transport_(cursor_, block);
            error = c2_result.error;
            if (!error && (c2_result.c2_status == C2Status::clean ||
                           c2_result.c2_status == C2Status::reported)) {
                block_c2 = c2_result.c2_status;
            } else {
                if (options_.require_c2_pointers) {
                    result.c2_status = C2Status::unknown;
                    result.status = ReadStatus::read_error;
                    result.native_error = error ? error : EPROTO;
                    positioned_ = false;
                    return result;
                }
                // A malformed success is no more meaningful than a packet
                // failure. Use normal audio extraction without claiming C2.
                block_c2 = C2Status::unknown;
                error = 0;
                unsigned attempt = 0;
                do {
                    error = transport_(cursor_, block);
                    if (!error) break;
                    if (error != EIO || attempt == options_.retries) break;
                    ++attempt;
                    ++result.retries;
                } while (true);
            }
        }
        if (!options_.request_c2_pointers || error) {
            if (options_.request_c2_pointers) block_c2 = C2Status::unknown;
            unsigned attempt = 0;
            do {
                error = transport_(cursor_, block);
                if (!error) break;
                // Only an I/O error gets optional retries; permissions, removal etc. fail immediately.
                if (error != EIO || attempt == options_.retries) break;
                ++attempt;
                ++result.retries;
            } while (true);
        }
        if (error) {
            positioned_ = false;
            result.status = ReadStatus::read_error;
            result.native_error = error;
            return result;
        }
        if (result.frames_read == 0) result.c2_status = block_c2;
        else if (result.c2_status == C2Status::reported || block_c2 == C2Status::reported)
            result.c2_status = C2Status::reported;
        else if (result.c2_status != block_c2)
            result.c2_status = C2Status::unknown;
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
    if (options.retries > 10 || options.c2_timeout_ms == 0 || options.c2_timeout_ms > 5000 ||
        (options.require_c2_pointers && !options.request_c2_pointers))
        throw std::invalid_argument("invalid direct reader options");
#ifdef ENABLE_PARANOIA
    if (backend == CddaBackend::paranoia) {
        if (options.retries != 0 || options.require_c2_pointers || options.c2_timeout_ms != 5000)
            throw std::invalid_argument("direct options cannot be used with paranoia");
        return make_paranoia_reader(device);
    }
#endif
    auto fd = std::make_shared<DeviceFd>(device);
    auto audio_read = [fd](std::int32_t lba, std::span<std::int16_t> pcm) {
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
    };
    C2AudioRead c2_read;
    if (options.request_c2_pointers) {
        c2_read = make_mmc_c2_audio_read([fd, timeout = options.c2_timeout_ms](std::span<const std::uint8_t> command,
                                             std::span<std::uint8_t> data,
                                             std::span<std::uint8_t> sense, std::size_t& transferred) {
            return picdplayer_detail::read_mmc_packet(fd->value, command, data, sense, transferred, timeout);
        });
    }
    return std::make_unique<LinuxIoctlReader>(std::move(audio_read), options, std::move(c2_read));
}
