#pragma once

#include "cdda_reader.hpp"
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

const char* read_verification_mode_name(ReadVerificationMode mode);
ReadVerificationMode parse_read_verification_mode(std::string_view value);
void validate_read_policy(const ReadPolicy& policy, std::size_t buffer_capacity_frames);
RepeatedReadPolicy repeated_read_policy(const ReadPolicy& policy);
std::string read_policy_strategy(const ReadPolicy& policy, CddaBackend backend);
