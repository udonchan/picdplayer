#include "integrity_state.hpp"

namespace {
bool backend_reported_verification(const ParanoiaEvents& events) {
    return events.verifies || events.fixups;
}
}

ReadEvidence make_read_evidence(const ReadResult& result) {
    ReadEvidence evidence;
    evidence.start_lba = result.start_lba;
    evidence.frames_requested = result.frames_requested;
    evidence.frames_read = result.frames_read;
    const bool complete = result.status == ReadStatus::ok &&
                          result.frames_read == result.frames_requested;
    const bool locally_verified = result.verification.matching_reads >= 2;
    const bool backend_recovered = result.paranoia.fixups && !result.paranoia.skips;
    const bool anomaly = result.retries || result.paranoia.skips ||
                         result.paranoia.read_errors || result.paranoia.cache_errors;
    if (!complete) evidence.status = IntegrityReadStatus::uncertain;
    else if (backend_recovered || (locally_verified && result.verification.mismatches))
        evidence.status = IntegrityReadStatus::recovered;
    else if (anomaly) evidence.status = IntegrityReadStatus::uncertain;
    else evidence.status = IntegrityReadStatus::clean;
    if (locally_verified) evidence.local_verification = LocalVerification::multiple_match;
    else if (backend_reported_verification(result.paranoia))
        evidence.local_verification = LocalVerification::backend_reported;
    else evidence.local_verification = LocalVerification::single_read;
    // Neither current backend requests C2 pointers or applies a configured
    // drive offset. UNKNOWN is more accurate than assuming absence or zero.
    evidence.c2_status = C2Status::not_checked;
    evidence.offset_status = OffsetStatus::unknown;
    evidence.direct_retries = result.retries;
    evidence.backend_events = result.paranoia;
    evidence.verification = result.verification;
    return evidence;
}

void observe_read(IntegrityStats& stats, const ReadResult& result) {
    ++stats.read_calls;
    stats.frames_requested += result.frames_requested;
    const bool accepted = result.status == ReadStatus::ok &&
                          result.frames_read == result.frames_requested;
    if (accepted) stats.frames_accepted += result.frames_read;
    stats.direct_retries += result.retries;
    stats.backend_reads += result.paranoia.reads;
    stats.backend_verifies += result.paranoia.verifies;
    stats.backend_fixups += result.paranoia.fixups;
    stats.backend_skips += result.paranoia.skips;
    stats.backend_read_errors += result.paranoia.read_errors;
    stats.backend_cache_errors += result.paranoia.cache_errors;
    stats.backend_other += result.paranoia.other;
    if (!accepted) ++stats.failed_calls;
    stats.verification_attempts += result.verification.attempts;
    stats.verification_mismatches += result.verification.mismatches;
    if (result.verification.matching_reads >= 2) ++stats.verified_calls;
    if (result.verification.attempts && !accepted) ++stats.verification_failures;
}

const char* read_activity_name(ReadActivity value) {
    switch (value) {
    case ReadActivity::idle: return "IDLE";
    case ReadActivity::reading: return "READING";
    case ReadActivity::buffering: return "BUFFERING";
    case ReadActivity::complete: return "COMPLETE";
    case ReadActivity::failed: return "FAILED";
    }
    return "IDLE";
}

const char* integrity_read_status_name(IntegrityReadStatus value) {
    switch (value) {
    case IntegrityReadStatus::unknown: return "UNKNOWN";
    case IntegrityReadStatus::clean: return "CLEAN";
    case IntegrityReadStatus::recovered: return "RECOVERED";
    case IntegrityReadStatus::uncertain: return "UNCERTAIN";
    }
    return "UNKNOWN";
}

const char* local_verification_name(LocalVerification value) {
    switch (value) {
    case LocalVerification::none: return "NONE";
    case LocalVerification::single_read: return "SINGLE_READ";
    case LocalVerification::backend_reported: return "BACKEND_REPORTED";
    case LocalVerification::multiple_match: return "MULTIPLE_MATCH";
    }
    return "NONE";
}

const char* c2_status_name(C2Status value) {
    switch (value) {
    case C2Status::unknown: return "UNKNOWN";
    case C2Status::not_available: return "NOT_AVAILABLE";
    case C2Status::not_checked: return "NOT_CHECKED";
    case C2Status::clean: return "CLEAN";
    case C2Status::reported: return "REPORTED";
    }
    return "UNKNOWN";
}

const char* offset_status_name(OffsetStatus value) {
    switch (value) {
    case OffsetStatus::unknown: return "UNKNOWN";
    case OffsetStatus::uncorrected: return "UNCORRECTED";
    case OffsetStatus::corrected: return "CORRECTED";
    }
    return "UNKNOWN";
}
