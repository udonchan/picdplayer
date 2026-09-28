#pragma once

#include <chrono>
#include <optional>

// Bounds provider backoff without extending metadata lookup indefinitely.
std::chrono::milliseconds metadata_retry_delay(std::optional<long long> retry_after_seconds);
