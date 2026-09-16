#include "musicbrainz_disc_id.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value) {
    if (!value) throw std::runtime_error("MusicBrainz Disc ID test failed");
}
template<class Function> void invalid(Function function) {
    try { function(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid TOC accepted");
}
}

int main() {
    try {
        // Official MusicBrainz Disc ID calculation example.
        const auto official = make_audio_toc(1,
            std::vector<std::int32_t>{0, 15213, 32164, 46442, 63264, 80339}, 95312);
        const auto result = calculate_musicbrainz_disc_id(official);
        check(result.id == "49HHV7Eb8UKF3aQiNmu1GR8vKTY-");
        check(result.toc == "1 6 95462 150 15363 32314 46592 63414 80489");

        const auto non_one = make_audio_toc(98, std::vector<std::int32_t>{0, 75}, 150);
        const auto non_one_result = calculate_musicbrainz_disc_id(non_one);
        check(non_one_result.id.size() == 28);
        check(non_one_result.toc == "98 99 300 150 225");

        auto inconsistent = official;
        ++inconsistent.tracks[0].length_frames;
        invalid([&] { (void)calculate_musicbrainz_disc_id(inconsistent); });
        const auto overflowing = make_audio_toc(1, std::vector<std::int32_t>{0},
                                                 std::numeric_limits<std::int32_t>::max());
        invalid([&] { (void)calculate_musicbrainz_disc_id(overflowing); });
        std::cout << "PASS: official Disc ID vector, numbering, validation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
