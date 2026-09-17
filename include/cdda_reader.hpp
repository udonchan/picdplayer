#pragma once
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

inline constexpr std::size_t cdda_samples_per_frame = 1176;
enum class CddaBackend { direct, paranoia };
CddaBackend parse_cdda_backend(std::string_view name);
void require_cdda_backend(CddaBackend backend);

enum class ReadStatus { ok, read_error };
struct ParanoiaEvents {
    unsigned reads = 0, verifies = 0, fixups = 0, skips = 0;
    unsigned read_errors = 0, cache_errors = 0, other = 0;
};
struct LocalReadVerification {
    unsigned attempts = 0;
    unsigned complete_reads = 0;
    unsigned matching_reads = 0;
    unsigned mismatches = 0;
    bool time_budget_exhausted = false;
};
struct ReadResult {
    std::int32_t start_lba;
    std::size_t frames_requested;
    std::size_t frames_read;
    ReadStatus status;
    int native_error; // Diagnostic only; callers use status for control flow.
    unsigned retries; // direct application retries only
    ParanoiaEvents paranoia{}; // callback counts, not sector/retry counts
    LocalReadVerification verification{};
};

struct RepeatedReadPolicy {
    unsigned required_matches = 2;
    unsigned maximum_attempts = 3;
    std::chrono::milliseconds time_budget{10000};
};
using SteadyNow = std::function<std::chrono::steady_clock::time_point()>;
void validate_repeated_read_policy(const RepeatedReadPolicy& policy);

class CddaReader {
public:
    virtual ~CddaReader() = default;
    virtual void seek(std::int32_t lba) = 0;
    // Interleaved stereo, signed 16-bit host endian, 44100Hz.
    // Nonempty buffer, whole CD frames only. Only frames_read are valid.
    // seek required initially and after read_error; no implicit EOF detection.
    virtual ReadResult read(std::span<std::int16_t> pcm) = 0;
};
std::unique_ptr<CddaReader> make_repeated_read_verifier(
    std::unique_ptr<CddaReader> reader, RepeatedReadPolicy policy = {},
    SteadyNow now = [] { return std::chrono::steady_clock::now(); });
struct DirectOptions {
    unsigned retries = 0; // Additional attempts per ioctl, bounded to 10.
};
std::unique_ptr<CddaReader> make_cdda_reader(CddaBackend backend,
    const std::string& device, DirectOptions options = {});
