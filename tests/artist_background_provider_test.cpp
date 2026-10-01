#include "artist_background_provider.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("artist background provider test failed"); }
constexpr auto artist = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
constexpr auto fixture = R"({"mbid_id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3","artistbackground":[{"id":"1","url":"https://assets.fanart.tv/fanart/music/0383dadf-2a4e-4d10-a46a-e9e041da8eb3/artistbackground/photo.jpg","width":"1920","height":"1080"}]})";
}

int main() {
    try {
        int calls = 0;
        FanartHttpGet get = [&](std::string_view url, std::string_view key, std::size_t limit,
                                const std::function<bool()>&) {
            ++calls;
            check(url == std::string("https://webservice.fanart.tv/v3.2/music/") + artist);
            check(key == "test-key" && limit == 512 * 1024);
            return HttpResponse{.status = 200, .content_type = "application/json", .body = fixture};
        };
        check(lookup_artist_backgrounds(artist, "", {}, get).status == ArtistBackgroundLookupStatus::disabled);
        check(calls == 0);
        const auto available = lookup_artist_backgrounds(artist, "test-key", {}, get);
        check(available.status == ArtistBackgroundLookupStatus::available && available.images.size() == 1);
        check(calls == 1);
        check(lookup_artist_backgrounds("invalid", "test-key", {}, get).status == ArtistBackgroundLookupStatus::error);
        check(lookup_artist_backgrounds(artist, "bad\nkey", {}, get).status == ArtistBackgroundLookupStatus::error);
        check(lookup_artist_backgrounds(artist, "test-key", [] { return true; }, get).status == ArtistBackgroundLookupStatus::error);
        check(calls == 1);
        FanartHttpGet missing = [](std::string_view, std::string_view, std::size_t,
                                   const std::function<bool()>&) {
            return HttpResponse{.status = 404};
        };
        check(lookup_artist_backgrounds(artist, "test-key", {}, missing).status == ArtistBackgroundLookupStatus::unavailable);
        FanartHttpGet rejected = [](std::string_view, std::string_view, std::size_t,
                                    const std::function<bool()>&) {
            return HttpResponse{.status = 401};
        };
        check(lookup_artist_backgrounds(artist, "test-key", {}, rejected).status == ArtistBackgroundLookupStatus::error);
        FanartHttpGet limited = [](std::string_view, std::string_view, std::size_t,
                                   const std::function<bool()>&) {
            return HttpResponse{.status = 429, .retry_after_seconds = 60};
        };
        check(lookup_artist_backgrounds(artist, "test-key", {}, limited).status == ArtistBackgroundLookupStatus::error);
        FanartHttpGet wrong_artist = [](std::string_view, std::string_view, std::size_t,
                                        const std::function<bool()>&) {
            return HttpResponse{.status = 200, .content_type = "application/json",
                .body = R"({"mbid_id":"89ad4ac3-39f7-470e-963a-56509c546377"})"};
        };
        check(lookup_artist_backgrounds(artist, "test-key", {}, wrong_artist).status == ArtistBackgroundLookupStatus::error);
        FanartHttpGet failing = [](std::string_view, std::string_view, std::size_t,
                                   const std::function<bool()>&) -> HttpResponse {
            throw std::runtime_error("simulated timeout");
        };
        check(lookup_artist_backgrounds(artist, "test-key", {}, failing).status == ArtistBackgroundLookupStatus::error);
        const auto root = std::filesystem::temp_directory_path() /
            ("picdplayer-artist-provider-" + std::to_string(::getpid()));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const auto cached_first = lookup_artist_backgrounds(artist, "test-key", {}, get, root);
        check(cached_first.status == ArtistBackgroundLookupStatus::available && calls == 2);
        check(lookup_artist_backgrounds(artist, "test-key", {}, failing, root).status ==
              ArtistBackgroundLookupStatus::available);
        check(lookup_artist_backgrounds(artist, "", {}, failing, root).status ==
              ArtistBackgroundLookupStatus::disabled);
        const auto json = root / "artist-background" / (std::string(artist) + ".json");
        std::filesystem::last_write_time(json,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(8));
        check(lookup_artist_backgrounds(artist, "test-key", {}, failing, root).status ==
              ArtistBackgroundLookupStatus::available);
        check(lookup_artist_backgrounds(artist, "test-key", {}, limited, root).status ==
              ArtistBackgroundLookupStatus::available);
        {
            std::ofstream corrupt(json, std::ios::trunc);
            corrupt << "invalid";
        }
        check(lookup_artist_backgrounds(artist, "test-key", {}, failing, root).status ==
              ArtistBackgroundLookupStatus::error);
        check(!std::filesystem::exists(json));
        check(lookup_artist_backgrounds(artist, "test-key", {}, missing, root).status ==
              ArtistBackgroundLookupStatus::unavailable);
        check(lookup_artist_backgrounds(artist, "test-key", {}, failing, root).status ==
              ArtistBackgroundLookupStatus::unavailable);
        std::filesystem::remove_all(root, ec);
        std::cout << "PASS: optional fanart.tv lookup and failure isolation\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
