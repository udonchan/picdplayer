#pragma once

#include "cdda_reader.hpp"
#include "drive_capabilities.hpp"
#include <cstddef>
#include <string_view>

enum class ReadVerificationMode { single, repeat };

struct ReadPolicy {
    ReadVerificationMode mode = ReadVerificationMode::single;
    std::size_t region_frames = 75;
    unsigned required_matches = 2;
    unsigned maximum_attempts = 3;
    unsigned time_budget_ms = 10'000;
};

// A requested strategy is the configuration the operator selected.  The
// effective strategy is the immutable configuration of the current (or next,
// while stopped) reader.  A downgrade never claims that returned PCM is bad;
// it only records why an optional read mechanism was not selected.
enum class ReadStrategyDowngrade {
    none,
    policy_restart_required,
    c2_probe_pending,
    c2_capability_unknown,
    c2_unsupported,
    c2_backend_unsupported,
    c2_stream_restart_required,
};

struct ReadStrategySelection {
    std::string requested;
    std::string effective;
    ReadStrategyDowngrade downgrade = ReadStrategyDowngrade::none;
    bool pending = false;
};

const char* read_verification_mode_name(ReadVerificationMode mode);
ReadVerificationMode parse_read_verification_mode(std::string_view value);
void validate_read_policy(const ReadPolicy& policy, std::size_t buffer_capacity_frames);
RepeatedReadPolicy repeated_read_policy(const ReadPolicy& policy);
std::string read_policy_strategy(const ReadPolicy& policy, CddaBackend backend);
ReadStrategySelection select_read_strategy(const ReadPolicy& policy, CddaBackend backend,
                                           bool request_c2_pointers, bool probe_complete,
                                           Knowledge c2_supported);
const char* read_strategy_downgrade_name(ReadStrategyDowngrade downgrade);
