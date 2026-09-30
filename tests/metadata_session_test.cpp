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
        current.candidates.resize(2);
        current.candidates[0].metadata.release_id = "release-a";
        current.candidates[1].metadata.release_id = "release-b";
        check(session.apply({request_b.generation, b, current}));
        check(session.snapshot().disc_id == "current");
        check(!session.select_candidate(request_a.generation, 0));
        check(!session.select_candidate(request_b.generation, 2));
        check(session.select_candidate(request_b.generation, 1));
        check(session.snapshot().status == MetadataStatus::available && session.snapshot().selected == 1);
        ArtworkInfo cover; cover.status = ArtworkStatus::available; cover.mime_type = "image/jpeg";
        check(!session.apply_artwork(request_a.generation, "release-b", cover));
        check(!session.apply_artwork(request_b.generation, "release-a", cover));
        check(session.apply_artwork(request_b.generation, "release-b", cover));
        check(session.snapshot().artwork.status == ArtworkStatus::available);
        check(session.select_candidate(request_b.generation, 0));
        check(session.snapshot().selected == 0 && session.snapshot().artwork.status == ArtworkStatus::not_requested);
        check(!session.apply_artwork(request_b.generation, "release-b", cover));
        MetadataResult failed; failed.status = MetadataStatus::error; failed.error = "simulated metadata timeout";
        const auto request_failure = session.begin(a);
        check(!session.apply({request_b.generation, b, failed}));
        check(session.apply({request_failure.generation, a, failed}));
        check(session.snapshot().status == MetadataStatus::error &&
              session.snapshot().error == "simulated metadata timeout");
        session.invalidate();
        check(!session.select_candidate(request_b.generation, 0));
        check(!session.apply_artwork(request_b.generation, "release-a", cover));
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
