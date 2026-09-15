#include "disc_toc.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>

void check(bool ok) { if (!ok) throw std::runtime_error("TOC test failed"); }
void invalid(int first, std::vector<std::int32_t> starts, std::int32_t end) {
    try { (void)make_audio_toc(first, starts, end); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid TOC accepted");
}
int main() {
    try {
        const std::vector<std::int32_t> starts{0,17607,34436,53083,66942,78857,94846,
            105012,121001,139129,161191,186744,218927,230183};
        const auto toc = make_audio_toc(1, starts, 242334);
        check(toc.tracks.size() == 14 && toc.span_frames() == 242334);
        check(toc.tracks.front().length_frames == 17607);
        check(toc.tracks.back().number == 14 && toc.tracks.back().length_frames == 12151);
        std::int64_t sum = 0;
        for (const auto& t : toc.tracks) sum += t.length_frames;
        check(sum == toc.span_frames());
        const auto single = make_audio_toc(99, std::vector<std::int32_t>{150}, 225);
        check(single.tracks[0].number == 99 && single.span_frames() == 75);
        const auto large = make_audio_toc(1, std::vector<std::int32_t>{0},
                                          std::numeric_limits<std::int32_t>::max());
        check(large.span_frames() == std::numeric_limits<std::int32_t>::max());
        invalid(1, {}, 100);
        invalid(0, {0}, 100);
        invalid(100, {0}, 100);
        invalid(99, {0, 50}, 100);
        invalid(1, {-1}, 100);
        invalid(1, {0, 0}, 100);
        invalid(1, {20, 10}, 100);
        invalid(1, {0, 50}, 50);
        invalid(1, {0}, -1);
        std::cout << "PASS: measured TOC, lengths, numbering, invalid snapshots\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
