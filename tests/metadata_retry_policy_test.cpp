#include "metadata_retry_policy.hpp"

#include <iostream>
#include <stdexcept>

namespace { void check(bool value) { if (!value) throw std::runtime_error("metadata retry policy test failed"); } }

int main() {
    try {
        check(metadata_retry_delay({}) == std::chrono::milliseconds(1100));
        check(metadata_retry_delay(0) == std::chrono::milliseconds(0));
        check(metadata_retry_delay(4) == std::chrono::seconds(4));
        check(metadata_retry_delay(99) == std::chrono::seconds(15));
        check(metadata_retry_delay(-1) == std::chrono::milliseconds(1100));
        std::cout << "PASS: metadata Retry-After backoff is bounded\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
