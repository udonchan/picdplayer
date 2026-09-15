#include "player_controller.hpp"
#include <limits>
#include <iostream>
#include <stdexcept>
void check(bool ok) { if (!ok) throw std::runtime_error("player state test failed"); }
int main() {
    try {
        PlayerController p;
        p.play(); p.pause(); p.stop(); p.next(); p.previous(); p.seek_relative(75);
        check(p.state().playback == PlaybackState::no_disc && !p.state().position_lba);
        check(!p.select_track(1));
        const auto toc = make_audio_toc(3, std::vector<std::int32_t>{150,900,1800}, 3000);
        p.load_disc(toc);
        check(p.state().playback == PlaybackState::stopped && p.state().track == 3);
        p.pause(); check(p.state().playback == PlaybackState::stopped);
        p.play(); p.seek_relative(100); p.pause();
        check(p.state().playback == PlaybackState::paused && p.state().position_lba == 250);
        p.play(); check(p.state().position_lba == 250);
        p.next(); check(p.state().track == 4 && p.state().position_lba == 900);
        check(p.state().playback == PlaybackState::playing);
        p.seek_relative(1000); check(p.state().track == 5 && p.state().position_lba == 1900);
        p.next(); check(p.state().position_lba == 1900);
        p.previous(); check(p.state().track == 4);
        p.stop(); check(p.state().playback == PlaybackState::stopped && p.state().track == 3 &&
                        p.state().position_lba == 150);
        check(!p.select_track(99) && p.state().track == 3);
        p.seek_relative(std::numeric_limits<std::int64_t>::max());
        check(p.state().position_lba == 2999);
        p.seek_relative(std::numeric_limits<std::int64_t>::min());
        check(p.state().position_lba == 150 && p.state().track == 3);
        p.previous(); check(p.state().track == 3);
        p.play(); p.pause(); p.select_track(5);
        check(p.state().playback == PlaybackState::paused);
        auto bad = toc; bad.tracks[0].length_frames = 1;
        bool rejected = false;
        try { p.load_disc(bad); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && p.state().track == 5);
        auto snapshot = p.state(); snapshot.track = 99;
        check(snapshot.track == 99 && p.state().track == 5);
        p.remove_disc(); check(!p.state().track && p.state().playback == PlaybackState::no_disc);
        p.load_disc(toc); check(p.state().track == 3 && p.state().playback == PlaybackState::stopped);
        // Previous uses the playback position, with an exact three-second boundary.
        for (auto mode : {PlaybackState::stopped, PlaybackState::playing, PlaybackState::paused}) {
            for (int elapsed : {0, 224, 225, 226}) {
                p.load_disc(toc);
                p.select_track(4);
                if (mode != PlaybackState::stopped) p.play();
                if (mode == PlaybackState::paused) p.pause();
                p.seek_relative(elapsed);
                p.previous();
                check(p.state().track == (elapsed < 225 ? 3 : 4));
                check(p.state().position_lba == (elapsed < 225 ? 150 : 900));
                check(p.state().playback == mode);
                p.previous();
                check(p.state().track == 3 && p.state().position_lba == 150);
                check(p.state().playback == mode);
                p.seek_relative(elapsed);
                p.previous();
                check(p.state().track == 3 && p.state().position_lba == 150);
                check(p.state().playback == mode);
            }
        }
        std::cout << "PASS: commands, boundaries, snapshots, media replacement\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
