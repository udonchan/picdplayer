#pragma once

#include "cdda_reader.hpp"
#include "read_policy.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Observable facts about the current read stream. These types deliberately do
// not claim that successfully returned PCM is the original disc PCM.
enum class ReadActivity { idle, reading, buffering, complete, failed };
enum class LocalVerification { none, single_read, backend_reported, multiple_match };
enum class IntegrityReadStatus { unknown, clean, recovered, uncertain };
enum class C2Status { unknown, not_available, not_checked, clean, reported };
enum class OffsetStatus { unknown, uncorrected, corrected };

struct ReadEvidence {
    std::uint64_t device_generation = 0; // successful reader-open incarnation, not hardware identity
    std::uint64_t disc_generation = 0;
    std::uint64_t read_sequence = 0;
    std::uint64_t stream_generation = 0;
    std::uint64_t policy_revision = 0;
    std::int32_t start_lba = 0;
    std::size_t frames_requested = 0;
    std::size_t frames_read = 0;
    IntegrityReadStatus status = IntegrityReadStatus::unknown;
    LocalVerification local_verification = LocalVerification::none;
    C2Status c2_status = C2Status::unknown;
    OffsetStatus offset_status = OffsetStatus::unknown;
    unsigned direct_retries = 0;
    ParanoiaEvents backend_events{};
    LocalReadVerification verification{};
};

struct IntegrityStats {
    std::uint64_t read_calls = 0;
    std::uint64_t frames_requested = 0;
    std::uint64_t frames_accepted = 0;
    std::uint64_t direct_retries = 0;
    std::uint64_t backend_reads = 0;
    std::uint64_t backend_verifies = 0;
    std::uint64_t backend_fixups = 0;
    std::uint64_t backend_skips = 0;
    std::uint64_t backend_read_errors = 0;
    std::uint64_t backend_cache_errors = 0;
    std::uint64_t backend_other = 0;
    std::uint64_t failed_calls = 0;
    std::uint64_t verification_attempts = 0;
    std::uint64_t verification_mismatches = 0;
    std::uint64_t verified_calls = 0;
    std::uint64_t verification_failures = 0;
};

// Fixed-memory union of accepted CD-frame intervals, scoped to one stream.
// 採用区間の和集合。上限到達後は下限値を固定し、未観測領域を成功扱いしない。
struct ReadCoverage {
    struct Region { std::int64_t begin = 0, end = 0; };
    static constexpr std::size_t capacity = 128;
    std::array<Region, capacity> regions{};
    std::size_t size = 0;
    std::uint64_t accepted_unique_frames = 0;
    bool complete = true; // all accepted observations counted, not whole-disc coverage
    void observe(const ReadResult& result);
};

// Bounded, disc-scoped observations. Flags describe facts on exact intervals.
// ディスク世代内の区間観測。上限超過時は下限の保持内容を固定する。
struct DiscReadMap {
    struct Region { std::int64_t begin = 0, end = 0; unsigned flags = 0; };
    static constexpr std::size_t capacity = 256;
    enum Flag : unsigned { attempted = 1, accepted = 2, retry = 4, repeated = 8,
                           recovered = 16, uncertain = 32, backend_anomaly = 64 };
    std::array<Region, capacity> regions{};
    std::size_t size = 0;
    std::uint64_t disc_generation = 0, revision = 0;
    bool complete = true;
    void observe(const ReadResult& result);
};

inline constexpr std::size_t read_history_capacity = 128;
struct ReadDiagnostics {
    std::string session_id;
    std::optional<DiscReadMap> disc_map; // on-demand only; independent of stream history
    bool history_included = false;
    std::vector<ReadEvidence> recent_reads; // populated only for API projection
    std::uint64_t history_evicted = 0;
    std::uint64_t stream_generation = 0;
    std::uint64_t policy_revision = 0;
    ReadCoverage coverage;
    ReadActivity activity = ReadActivity::idle;
    std::string requested_mode = "LEGACY";
    std::string effective_strategy = "legacy";
    std::optional<ReadEvidence> latest;
    std::optional<ReadEvidence> current_playback;
    std::optional<ReadEvidence> active_warning; // last uncertain read in this stream
    std::size_t queued_blocks = 0;
    std::size_t buffer_capacity_frames = 0;
    std::size_t startup_buffer_frames = 0;
    std::size_t read_block_frames = 0;
    std::size_t prebuffer_target_frames = 0;
    std::optional<std::int64_t> last_prebuffer_wait_ms;
    ReadPolicy requested_policy{};
    ReadPolicy effective_policy{};
    bool policy_pending = false;
    std::uint64_t dropped_events = 0;
    IntegrityStats stats;
};

ReadEvidence make_read_evidence(const ReadResult& result);
void observe_read(IntegrityStats& stats, const ReadResult& result);

const char* read_activity_name(ReadActivity value);
const char* integrity_read_status_name(IntegrityReadStatus value);
const char* local_verification_name(LocalVerification value);
const char* c2_status_name(C2Status value);
const char* offset_status_name(OffsetStatus value);
