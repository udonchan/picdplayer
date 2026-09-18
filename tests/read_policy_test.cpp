#include "read_policy.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("read policy test failed"); }
template<class F> void rejects(F function) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid policy accepted");
}
}

int main() {
    try {
        check(parse_read_verification_mode("single") == ReadVerificationMode::single);
        check(parse_read_verification_mode("repeat") == ReadVerificationMode::repeat);
        rejects([] { parse_read_verification_mode("unknown"); });
        const ReadPolicy single{};
        validate_read_policy(single, 300);
        const ReadPolicy repeat{ReadVerificationMode::repeat, 75, 2, 3, 10000};
        validate_read_policy(repeat, 300);
        const auto repeated = repeated_read_policy(repeat);
        check(repeated.required_matches == 2 && repeated.maximum_attempts == 3);
        check(read_policy_strategy(repeat, CddaBackend::direct) == "direct+repeat-2of3");
        rejects([&] { validate_read_policy({ReadVerificationMode::repeat, 14, 2, 3, 10000}, 300); });
        rejects([&] { validate_read_policy({ReadVerificationMode::repeat, 315, 2, 3, 10000}, 300); });
        rejects([&] { validate_read_policy({ReadVerificationMode::repeat, 75, 3, 2, 10000}, 300); });
        rejects([&] { validate_read_policy({ReadVerificationMode::single, 14, 2, 3, 10000}, 300); });
        std::cout << "PASS: read policy parsing, validation and strategy\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
