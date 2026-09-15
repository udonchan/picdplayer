#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

inline constexpr std::size_t cdda_samples_per_frame = 1176;
enum class CddaBackend { direct, paranoia };
CddaBackend parse_cdda_backend(std::string_view name);
void require_cdda_backend(CddaBackend backend);

enum class ReadStatus { ok, read_error };
struct ReadResult {
    std::int32_t start_lba;
    std::size_t frames_requested;
    std::size_t frames_read;
    ReadStatus status;
    int native_error; // Diagnostic only; callers use status for control flow.
    unsigned retries;
};

class CddaReader {
public:
    virtual ~CddaReader() = default;
    virtual void seek(std::int32_t lba) = 0;
    // Interleaved stereo, signed 16-bit host endian, 44100Hz.
    // Nonempty buffer, whole CD frames only. Only frames_read are valid.
    // seek required initially and after read_error; no implicit EOF detection.
    virtual ReadResult read(std::span<std::int16_t> pcm) = 0;
};
struct DirectOptions {
    unsigned retries = 0; // Additional attempts per ioctl, bounded to 10.
};
std::unique_ptr<CddaReader> make_cdda_reader(CddaBackend backend,
    const std::string& device, DirectOptions options = {});
