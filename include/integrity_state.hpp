#pragma once

#include "cdda_reader.hpp"
#include <cstdint>
#include <optional>
#include <string>

// Observable facts about the current read stream. These types deliberately do
// not claim that successfully returned PCM is the original disc PCM.
enum class ReadActivity { idle, reading, buffering, complete, failed };
enum class LocalVerification { none, single_read, backend_reported };
enum class IntegrityReadStatus { unknown, clean, recovered, uncertain };
enum class C2Status { unknown, not_available, not_checked, clean, reported };
enum class OffsetStatus { unknown, uncorrected, corrected };

struct ReadEvidence {
    std::int32_t start_lba = 0;
    std::size_t frames_requested = 0;
    std::size_t frames_read = 0;
    IntegrityReadStatus status = IntegrityReadStatus::unknown;
    LocalVerification local_verification = LocalVerification::none;
    C2Status c2_status = C2Status::unknown;
    OffsetStatus offset_status = OffsetStatus::unknown;
    unsigned direct_retries = 0;
    ParanoiaEvents backend_events{};
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
};

struct ReadDiagnostics {
    ReadActivity activity = ReadActivity::idle;
    std::string requested_mode = "LEGACY";
    std::string effective_strategy = "legacy";
    std::optional<ReadEvidence> latest;
    std::optional<ReadEvidence> current_playback;
    std::size_t queued_blocks = 0;
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
