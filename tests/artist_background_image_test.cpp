#include "artist_background_image.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <unistd.h>

namespace {
void check(bool okay) { if (!okay) throw std::runtime_error("artist background image test failed"); }
HttpResponse response(long status, std::string mime = {}, std::string body = {}) {
    HttpResponse result;
    result.status = status;
    result.content_type = std::move(mime);
    result.body = std::move(body);
    return result;
}
constexpr auto artist = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
const std::string jpeg = std::string("\xff\xd8", 2) + "fixture" + std::string("\xff\xd9", 2);
ArtistBackgroundCandidate candidate(std::string name = "photo.jpg") {
    return {"123", std::string("https://assets.fanart.tv/fanart/music/") + artist +
        "/artistbackground/" + name, 1920, 1080};
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("picdplayer-artist-image-" + std::to_string(::getpid()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    try {
        int calls = 0;
        ArtistImageHttpGet image_get = [&](std::string_view, std::size_t limit,
                                           const std::function<bool()>&) {
            ++calls;
            check(limit == 8 * 1024 * 1024);
            return response(200, "image/jpeg", jpeg);
        };
        const auto first = load_artist_background_image(artist, candidate(), root, {}, image_get);
        check(first && first->mime_type == "image/jpeg" && first->bytes == jpeg && calls == 1);
        ArtistImageHttpGet offline = [](std::string_view, std::size_t,
                                        const std::function<bool()>&) -> HttpResponse {
            throw std::runtime_error("offline");
        };
        check(load_artist_background_image(artist, candidate(), root, {}, offline)->bytes == jpeg);
        const auto directory = root / "artist-background";
        std::filesystem::path cached;
        for (const auto& file : std::filesystem::directory_iterator(directory))
            if (file.path().extension() == ".image") cached = file.path();
        check(!cached.empty());
        std::filesystem::last_write_time(cached,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
        check(load_artist_background_image(artist, candidate(), root, {}, offline)->bytes == jpeg);
        check(load_artist_background_image(artist, candidate("new.jpg"), root, {}, image_get)->bytes == jpeg);
        check(calls == 2);
        {
            std::ofstream out(cached, std::ios::binary | std::ios::trunc);
            out << "broken";
        }
        check(!load_artist_background_image(artist, candidate(), root, {}, offline));
        check(!std::filesystem::exists(cached));
        check(load_artist_background_image(artist, candidate(), root, {}, image_get)->bytes == jpeg);
        std::filesystem::last_write_time(cached,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
        ArtistImageHttpGet removed = [](std::string_view, std::size_t,
                                        const std::function<bool()>&) {
            return response(404);
        };
        check(!load_artist_background_image(artist, candidate(), root, {}, removed));
        check(!std::filesystem::exists(cached));
        auto unsafe = candidate("../escape.jpg");
        check(!load_artist_background_image(artist, unsafe, root, {}, image_get));
        unsafe = candidate("https://evil.example/photo.jpg");
        check(!load_artist_background_image(artist, unsafe, root, {}, image_get));
        check(calls == 3);
        ArtistImageHttpGet mismatch = [](std::string_view, std::size_t,
                                         const std::function<bool()>&) {
            return response(200, "image/png", jpeg);
        };
        check(!load_artist_background_image(artist, candidate(), {}, {}, mismatch));
        ArtistImageHttpGet truncated = [](std::string_view, std::size_t,
                                          const std::function<bool()>&) {
            return response(200, "image/jpeg", std::string("\xff\xd8truncated", 11));
        };
        check(!load_artist_background_image(artist, candidate(), {}, {}, truncated));
        const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) +
            std::string("\0\0\0\0IEND\0\0\0\0", 12);
        ArtistImageHttpGet png_get = [&](std::string_view, std::size_t,
                                         const std::function<bool()>&) {
            return response(200, "image/png", png);
        };
        check(load_artist_background_image(artist, candidate(), {}, {}, png_get)->mime_type == "image/png");
        const std::string webp = std::string("RIFF", 4) + std::string("\x04\0\0\0", 4) + "WEBP";
        ArtistImageHttpGet webp_get = [&](std::string_view, std::size_t,
                                          const std::function<bool()>&) {
            return response(200, "image/webp", webp);
        };
        check(load_artist_background_image(artist, candidate(), {}, {}, webp_get)->mime_type == "image/webp");
        check(!load_artist_background_image(artist, candidate(), {}, [] { return true; }, image_get));
        bool rejected = false;
        try {
            (void)HttpClient{}.get("https://assets.fanart.tv.evil.example/fanart/music/x",
                                   100, {}, RedirectPolicy::fanart_asset);
        } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        std::filesystem::remove_all(root, error);
        std::cout << "PASS: bounded artist image fetch and cache\n";
    } catch (const std::exception& failure) {
        std::filesystem::remove_all(root, error);
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
