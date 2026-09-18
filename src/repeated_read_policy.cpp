#include "read_policy.hpp"

#include "pcm_worker.hpp"
#include <chrono>
#include <stdexcept>
#include <string>

const char* read_verification_mode_name(ReadVerificationMode mode) {
    switch (mode) {
    case ReadVerificationMode::single: return "SINGLE";
    case ReadVerificationMode::repeat: return "REPEAT";
    }
    return "SINGLE";
}

ReadVerificationMode parse_read_verification_mode(std::string_view value) {
    if (value == "single") return ReadVerificationMode::single;
    if (value == "repeat") return ReadVerificationMode::repeat;
    throw std::invalid_argument("read verification mode must be single or repeat");
}

void validate_read_policy(const ReadPolicy& policy, std::size_t buffer_capacity_frames) {
    if (policy.region_frames < pcm_block_cd_frames ||
        policy.region_frames > buffer_capacity_frames ||
        policy.region_frames % pcm_block_cd_frames != 0)
        throw std::invalid_argument("region_frames must be a multiple of 15 and fit the read buffer");
    validate_repeated_read_policy({policy.required_matches, policy.maximum_attempts,
                                   std::chrono::milliseconds(policy.time_budget_ms)});
}

RepeatedReadPolicy repeated_read_policy(const ReadPolicy& policy) {
    if (policy.mode != ReadVerificationMode::repeat)
        throw std::invalid_argument("single policy has no repeated read policy");
    return {policy.required_matches, policy.maximum_attempts,
            std::chrono::milliseconds(policy.time_budget_ms)};
}

std::string read_policy_strategy(const ReadPolicy& policy, CddaBackend backend) {
    const char* base = backend == CddaBackend::direct ? "direct" : "paranoia-library";
    if (policy.mode == ReadVerificationMode::single) return std::string(base) + "-single-read";
    return std::string(base) + "+repeat-" + std::to_string(policy.required_matches) + "of" +
           std::to_string(policy.maximum_attempts);
}
