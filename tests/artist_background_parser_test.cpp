#include "artist_background_parser.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool okay) { if (!okay) throw std::runtime_error("artist background parser test failed"); }
constexpr auto artist = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
constexpr auto prefix = "https://assets.fanart.tv/fanart/music/0383dadf-2a4e-4d10-a46a-e9e041da8eb3/artistbackground/";
}

int main() {
    try {
        const std::string body = std::string(R"({"mbid_id":")") + artist +
            R"(","artistbackground":[{"id":"1","url":")" + prefix +
            R"(one.jpg","width":"1920","height":"1080"},{"id":"2","url":")" + prefix +
            R"(two.jpg","width":1600,"height":900},{"id":"3","url":"https://evil.example/image.jpg","width":"1920","height":"1080"}]})";
        const auto images = parse_artist_backgrounds(body, artist);
        check(images.size() == 2 && images[0].id == "1" && images[1].id == "2");
        check(images[0].width == 1920 && images[0].height == 1080);
        const std::string traversal = std::string(R"({"mbid_id":")") + artist +
            R"(","artistbackground":[{"id":"4","url":")" + prefix +
            R"(../other.jpg","width":"1920","height":"1080"}]})";
        check(parse_artist_backgrounds(traversal, artist).empty());
        check(parse_artist_backgrounds(std::string(R"({"mbid_id":")") + artist + R"("})", artist).empty());
        bool mismatch = false;
        try { (void)parse_artist_backgrounds(body, "89ad4ac3-39f7-470e-963a-56509c546377"); }
        catch (const std::runtime_error&) { mismatch = true; }
        check(mismatch);
        bool invalid = false;
        try { (void)parse_artist_backgrounds("{}", "../artist"); }
        catch (const std::invalid_argument&) { invalid = true; }
        check(invalid);
        std::cout << "PASS: bounded fanart.tv artist background parsing\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
