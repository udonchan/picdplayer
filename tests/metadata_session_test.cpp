#include "metadata_session.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace { void check(bool value) { if (!value) throw std::runtime_error("metadata session test failed"); } }
int main() {
    try {
        const auto a = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
        const auto b = make_audio_toc(1, std::vector<std::int32_t>{0, 75}, 150);
        MetadataSession session;
        auto request_a = session.begin(a);
        auto request_b = session.begin(b);
        MetadataResult old; old.status = MetadataStatus::available; old.disc_id = "old";
        check(!session.apply({request_a.generation, a, old}));
        MetadataResult current; current.status = MetadataStatus::ambiguous; current.disc_id = "current";
        check(session.apply({request_b.generation, b, current}));
        check(session.snapshot().disc_id == "current");
        session.invalidate();
        auto request_a_again = session.begin(a);
        check(!session.apply({request_a.generation, a, old}));
        check(session.apply({request_a_again.generation, a, old}));
        check(!session.begin_if_needed(a)); // A ready result is not requested again.
        session.invalidate(); // Temporary LOADING may return the same TOC.
        const auto refreshed = session.begin_if_needed(a);
        check(refreshed.has_value());
        check(!session.begin_if_needed(a)); // Nor is an in-flight request duplicated.
        check(!session.apply({request_a_again.generation, a, old}));
        check(session.apply({refreshed->generation, a, old}));
        check(session.begin_if_needed(b).has_value());
        std::cout << "PASS: stale metadata generations and TOCs are rejected\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
