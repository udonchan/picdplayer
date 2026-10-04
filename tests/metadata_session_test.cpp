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
        current.candidates[0].metadata.artist_identity = {ArtistIdentityStatus::available,
            "0383dadf-2a4e-4d10-a46a-e9e041da8eb3"};
        current.candidates[1].metadata.artist_identity = {ArtistIdentityStatus::available,
            "89ad4ac3-39f7-470e-963a-56509c546378"};
        check(session.apply({request_b.generation, b, current}));
        check(session.snapshot().disc_id == "current");
        check(selected_artist_identity(session.snapshot()).status == ArtistIdentityStatus::unavailable);
        check(!session.select_candidate(request_a.generation, 0));
        check(!session.select_candidate(request_b.generation, 2));
        check(session.select_candidate(request_b.generation, 1));
        check(session.snapshot().status == MetadataStatus::available && session.snapshot().selected == 1);
        check(selected_artist_identity(session.snapshot()).mbid ==
              "89ad4ac3-39f7-470e-963a-56509c546378");
        ArtworkInfo cover; cover.status = ArtworkStatus::available; cover.mime_type = "image/jpeg";
        check(!session.apply_artwork(request_a.generation, "release-b", cover));
        check(!session.apply_artwork(request_b.generation, "release-a", cover));
        check(session.apply_artwork(request_b.generation, "release-b", cover));
        check(session.snapshot().artwork.status == ArtworkStatus::available);
        check(session.select_candidate(request_b.generation, 0));
        check(session.snapshot().selected == 0 && session.snapshot().artwork.status == ArtworkStatus::not_requested);
        check(selected_artist_identity(session.snapshot()).mbid ==
              "0383dadf-2a4e-4d10-a46a-e9e041da8eb3");
        check(!session.apply_artwork(request_b.generation, "release-b", cover));
        check(!session.decline_candidates(request_a.generation));
        check(session.decline_candidates(request_b.generation));
        check(session.snapshot().status == MetadataStatus::ambiguous &&
              !session.snapshot().selected && session.snapshot().candidates_declined &&
              session.snapshot().artwork.status == ArtworkStatus::not_requested);
        check(!session.apply_artwork(request_b.generation, "release-a", cover));
        check(session.select_candidate(request_b.generation, 1));
        check(session.snapshot().selected == 1 && !session.snapshot().candidates_declined);
        MetadataResult failed; failed.status = MetadataStatus::error; failed.error = "simulated metadata timeout";
        const auto request_failure = session.begin(a);
        check(!session.apply({request_b.generation, b, failed}));
        check(session.apply({request_failure.generation, a, failed}));
        check(session.snapshot().status == MetadataStatus::error &&
              session.snapshot().error == "simulated metadata timeout");
        check(selected_artist_identity(session.snapshot()).status == ArtistIdentityStatus::unavailable);
        session.invalidate();
        check(selected_artist_identity(session.snapshot()).status == ArtistIdentityStatus::unavailable);
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
        MetadataSession progressive;
        MetadataResult single; single.status = MetadataStatus::available; single.selected = 0;
        single.candidates.resize(1); single.candidates[0].metadata.release_id = "release-single";
        const auto single_request = progressive.begin(a);
        check(progressive.apply({single_request.generation, a, single}));
        check(progressive.snapshot().status == MetadataStatus::available &&
              progressive.snapshot().artwork.status == ArtworkStatus::not_requested);
        ArtworkInfo failed_cover; failed_cover.status = ArtworkStatus::error;
        check(progressive.apply_artwork(single_request.generation, "release-single", failed_cover));
        check(progressive.snapshot().status == MetadataStatus::available &&
              progressive.snapshot().artwork.status == ArtworkStatus::error);
        progressive.begin(b);
        check(!progressive.apply_artwork(single_request.generation, "release-single", cover));
        std::cout << "PASS: stale metadata generations and TOCs are rejected\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
