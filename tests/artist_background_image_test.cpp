#include "artist_background_image.hpp"
#include "artist_background_image_fixtures.hpp"

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
using artist_background_image_fixtures::jpeg;
using artist_background_image_fixtures::png;
using artist_background_image_fixtures::webp;
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
            return response(200, "image/jpeg", jpeg.substr(0, jpeg.size() - 2));
        };
        check(!load_artist_background_image(artist, candidate(), {}, {}, truncated));
        ArtistImageHttpGet png_get = [&](std::string_view, std::size_t,
                                         const std::function<bool()>&) {
            return response(200, "image/png", png);
        };
        const auto png_image = load_artist_background_image(artist, candidate(), {}, {}, png_get);
        check(png_image && png_image->mime_type == "image/png" && png_image->bytes == png);
        ArtistImageHttpGet webp_get = [&](std::string_view, std::size_t,
                                          const std::function<bool()>&) {
            return response(200, "image/webp", webp);
        };
        const auto webp_image = load_artist_background_image(artist, candidate(), {}, {}, webp_get);
        check(webp_image && webp_image->mime_type == "image/webp" && webp_image->bytes == webp);
        check(!load_artist_background_image(artist, candidate(), {}, [] { return true; }, image_get));
        bool rejected = false;
        try {
            (void)HttpClient{}.get("https://assets.fanart.tv.evil.example/fanart/music/x",
                                   100, {}, RedirectPolicy::fanart_asset);
        } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        auto opaque = candidate();
        opaque.url = "https://assets.fanart.tv/fanart/music/backgrounds/opaque-photo.jpg";
        check(load_artist_background_image(artist, opaque, {}, {}, image_get)->bytes == jpeg);
        opaque.url = "https://assets.fanart.tv/fanart/flat-photo.jpg";
        ArtistImageHttpGet flat_get = [&](std::string_view url, std::size_t limit, const std::function<bool()>&) {
            check(url == opaque.url && limit == 8 * 1024 * 1024);
            return response(200, "image/jpeg", jpeg);
        };
        check(load_artist_background_image(artist, opaque, {}, {}, flat_get)->bytes == jpeg);
        for (const auto* url : {"http://assets.fanart.tv/fanart/music/photo.jpg",
                               "https://assets.fanart.tv.evil.example/fanart/music/photo.jpg",
                               "https://assets.fanart.tv/fanart/music/../photo.jpg",
                               "https://assets.fanart.tv/fanart/music/%2e%2e/photo.jpg",
                               "https://assets.fanart.tv/fanart/music/photo.jpg?token=x",
                               "https://assets.fanart.tv/fanart/flat-photo.jpg?token=x",
                               "https://assets.fanart.tv/fanart/movies/flat-photo.jpg",
                               "https://assets.fanart.tv/fanart/../flat-photo.jpg"}) {
            opaque.url = url;
            const auto before = calls;
            check(!load_artist_background_image(artist, opaque, {}, {}, image_get));
            check(calls == before);
            bool denied = false;
            try { (void)HttpClient{}.get(url, 100, {}, RedirectPolicy::fanart_asset); }
            catch (const std::invalid_argument&) { denied = true; }
            check(denied);
        }

        using artist_background_image_fixtures::make_png;
        using artist_background_image_fixtures::png_chunk;
        using artist_background_image_fixtures::png_u32;
        unsigned png_cases = 0;
        const auto png_case = [&](std::string_view name, const std::string& bytes, bool expected) {
            const auto loaded = load_artist_background_image(artist, candidate(), {}, {},
                [&](auto, auto, const auto&) { return response(200, "image/png", bytes); });
            if (loaded.has_value() != expected ||
                (loaded && (loaded->mime_type != "image/png" || loaded->bytes != bytes)))
                throw std::runtime_error("PNG case failed: " + std::string(name) +
                    " (" + std::to_string(bytes.size()) + " bytes)");
            ++png_cases;
        };
        const auto signature = png.substr(0, 8);
        const auto header = png.substr(8, 25);
        const auto tail = png.substr(33);
        const auto with_header = [&](const std::string& payload) {
            return signature + png_chunk("IHDR", payload) + tail;
        };
        check(png_chunk("IEND", {}) == png.substr(png.size() - 12));
        check(make_png(1, 1, 8, 2).substr(0, 33) == png.substr(0, 33));
        for (const unsigned color : {0U, 3U})
            for (const unsigned depth : {1U, 2U, 4U, 8U})
                png_case("low-depth static", make_png(1, 1, depth, color), true);
        for (const unsigned color : {2U, 4U, 6U})
            png_case("8-bit static", make_png(1, 1, 8, color), true);
        png_case("axis width boundary", make_png(4096, 1), true);
        png_case("axis height boundary", make_png(1, 4096), true);
        png_case("pixel boundary", make_png(4096, 2048), true);
        png_case("transposed pixel boundary", make_png(2048, 4096), true);
        png_case("4K static", make_png(3840, 2160), true);
        const auto over_width = make_png(4097, 1);
        png_case("axis width exceeded", over_width, false);
        png_case("axis height exceeded", make_png(1, 4097), false);
        png_case("pixel budget exceeded", make_png(4096, 2049), false);
        png_case("valid 16-bit outside product policy", make_png(1, 1, 16), false);
        for (std::size_t size = 0; size < png.size(); ++size)
            png_case("truncated framing", png.substr(0, size), false);
        for (const auto width : {0U, 4097U, 0xffffffffU}) {
            auto payload = png.substr(16, 13);
            payload.replace(0, 4, png_u32(width));
            png_case("structural width field", with_header(payload), false);
        }
        for (const auto field : {8U, 9U, 10U, 11U, 12U}) {
            auto payload = png.substr(16, 13);
            payload[field] = field == 8 ? 3 : field == 9 ? 5 : 2;
            png_case("illegal IHDR combination", with_header(payload), false);
        }
        auto adam7 = png.substr(16, 13);
        adam7[12] = 1; // A 1x1 image occupies only the first Adam7 pass.
        png_case("1x1 Adam7", with_header(adam7), true);
        png_case("short IHDR", with_header(png.substr(16, 12)), false);
        png_case("long IHDR", with_header(png.substr(16, 13) + 'x'), false);
        png_case("duplicate IHDR", signature + header + header + tail, false);
        png_case("missing IHDR", signature + tail, false);
        png_case("unknown critical", signature + header + png_chunk("ABCD", {}) + tail, false);
        png_case("non-letter type", signature + header + png_chunk("r1Nd", {}) + tail, false);
        png_case("reserved bit", signature + header + png_chunk("rand", {}) + tail, false);
        png_case("opaque ancillary", signature + header +
            png_chunk("tEXt", std::string("note") + '\0' + "acTL fcTL fdAT") + tail, true);
        auto corrupt = png;
        corrupt.back() ^= 1;
        png_case("IEND CRC", corrupt, false);
        auto bad_chunk = png_chunk("raNd", "x");
        bad_chunk.back() ^= 1;
        png_case("ancillary CRC", signature + header + bad_chunk + tail, false);
        for (const auto length : {0x7fffffffU, 0x80000000U, 0xffffffffU}) {
            auto invalid_length = png;
            invalid_length.replace(8, 4, png_u32(length));
            png_case("out-of-buffer chunk length", invalid_length, false);
        }
        const auto small = make_png(1, 1);
        const auto prefix = small.substr(0, 33);
        const auto packed = small.substr(41, small.size() - 57);
        const auto end = png_chunk("IEND", {});
        png_case("missing IDAT", prefix + end, false);
        png_case("empty IDAT stream", prefix + png_chunk("IDAT", {}) + end, false);
        png_case("zero-length IDAT among data", prefix + png_chunk("IDAT", {}) +
            png_chunk("IDAT", packed) + png_chunk("IDAT", {}) + end, true);
        const auto split = packed.size() / 2;
        png_case("consecutive split IDAT", prefix + png_chunk("IDAT", packed.substr(0, split)) +
            png_chunk("IDAT", packed.substr(split)) + end, true);
        png_case("nonconsecutive IDAT", prefix + png_chunk("IDAT", packed.substr(0, split)) +
            png_chunk("raNd", {}) + png_chunk("IDAT", packed.substr(split)) + end, false);
        png_case("missing IEND", png.substr(0, png.size() - 12), false);
        png_case("nonempty IEND", png.substr(0, png.size() - 12) + png_chunk("IEND", "x"), false);
        png_case("duplicate IEND", png + end, false);
        png_case("trailing bytes outside product policy", png + 'x', false);
        const auto indexed = make_png(1, 1, 1, 3);
        const auto indexed_prefix = indexed.substr(0, 33);
        const auto palette = indexed.substr(33, 18);
        const auto indexed_tail = indexed.substr(51);
        png_case("missing PLTE", indexed_prefix + indexed_tail, false);
        png_case("duplicate PLTE", indexed_prefix + palette + palette + indexed_tail, false);
        for (const auto size : {0U, 4U, 9U, 771U})
            png_case("invalid palette entries", indexed_prefix +
                png_chunk("PLTE", std::string(size, '\0')) + indexed_tail, false);
        png_case("palette forbidden for grayscale", prefix + palette + small.substr(33), false);
        const auto rgb = make_png(1, 1, 8, 2);
        png_case("PLTE after IDAT", rgb.substr(0, rgb.size() - 12) + palette + end, false);
        png_case("suggested truecolor palette", rgb.substr(0, 33) + palette + rgb.substr(33), true);
        const auto control = png_u32(0) + png_u32(1) + png_u32(1) + png_u32(0) + png_u32(0) +
            std::string("\x00\x01\x00\x64\x00\x00", 6);
        const auto animation = png_chunk("acTL", png_u32(1) + png_u32(0));
        png_case("one-frame APNG outside product policy", prefix + animation +
            png_chunk("fcTL", control) + small.substr(33), false);
        png_case("orphan fcTL policy", prefix + png_chunk("fcTL", control) + small.substr(33), false);
        png_case("orphan fdAT policy", small.substr(0, small.size() - 12) +
            png_chunk("fdAT", png_u32(0) + packed) + end, false);
        png_case("late acTL policy", small.substr(0, small.size() - 12) + animation + end, false);
        const auto padded = [&](std::size_t size) {
            return prefix + png_chunk("raNd", std::string(size - small.size() - 12, '\0')) + small.substr(33);
        };
        png_case("encoded limit", padded(artist_background_image_limit), true);
        png_case("encoded limit exceeded", padded(artist_background_image_limit + 1), false);

        const auto png_root = root / "png-admission";
        int png_calls = 0;
        ArtistImageHttpGet valid_png = [&](auto, auto, const auto&) {
            ++png_calls;
            return response(200, "image/png", png);
        };
        ArtistImageHttpGet unavailable_png = [&](auto, auto, const auto&) -> HttpResponse {
            ++png_calls;
            throw std::runtime_error("offline PNG");
        };
        check(load_artist_background_image(artist, candidate(), png_root, {}, valid_png)->bytes == png);
        check(load_artist_background_image(artist, candidate(), png_root, {}, unavailable_png)->bytes == png);
        check(png_calls == 1);
        std::filesystem::path png_cache;
        for (const auto& file : std::filesystem::directory_iterator(png_root / "artist-background"))
            if (file.path().extension() == ".image") png_cache = file.path();
        check(!png_cache.empty());
        std::filesystem::last_write_time(png_cache,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
        check(load_artist_background_image(artist, candidate(), png_root, {}, unavailable_png)->bytes == png);
        check(png_calls == 2);
        for (const bool stale : {false, true}) {
            check(load_artist_background_image(artist, candidate(), png_root, {}, valid_png)->bytes == png);
            {
                std::ofstream out(png_cache, std::ios::binary | std::ios::trunc);
                out.write(over_width.data(), static_cast<std::streamsize>(over_width.size()));
                check(static_cast<bool>(out));
            }
            if (stale) std::filesystem::last_write_time(png_cache,
                std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
            const auto before = png_calls;
            check(!load_artist_background_image(artist, candidate(), png_root, {}, unavailable_png));
            check(png_calls == before + 1 && !std::filesystem::exists(png_cache));
        }
        bool cancelled_png = false;
        check(!load_artist_background_image(artist, candidate(), {}, [&] { return cancelled_png; },
            [&](auto, auto, const auto&) {
                cancelled_png = true;
                return response(200, "image/png", png);
            }));
        std::filesystem::remove_all(root, error);
        std::cout << "PASS: bounded artist image fetch and cache (" << png_cases << " PNG cases)\n";
    } catch (const std::exception& failure) {
        std::filesystem::remove_all(root, error);
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
