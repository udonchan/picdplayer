#include "metadata_retry_policy.hpp"

#include <algorithm>

std::chrono::milliseconds metadata_retry_delay(std::optional<long long> retry_after_seconds) {
    constexpr auto fallback = std::chrono::milliseconds(1100);
    constexpr auto maximum = std::chrono::seconds(15);
    if (!retry_after_seconds || *retry_after_seconds < 0) return fallback;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::min(std::chrono::seconds(*retry_after_seconds), maximum));
}
