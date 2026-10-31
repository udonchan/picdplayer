#include "artist_background_image.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("WebP invariant failed"); }
struct Chunk { std::string type, payload; };
std::string uint_le(std::uint32_t value, unsigned count = 4) {
    std::string bytes;
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<char>((value >> (8 * i)) & 255));
    return bytes;
}
std::uint32_t read_le(std::string_view bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes.at(offset + i))) << (8 * i);
    return value;
}
std::vector<Chunk> unpack(std::string_view bytes) {
    std::vector<Chunk> chunks;
    for (std::size_t offset = 12; offset < bytes.size();) {
        check(bytes.size() - offset >= 8);
        const auto length = read_le(bytes, offset + 4);
        check(length <= bytes.size() - offset - 8);
        chunks.push_back({std::string(bytes.substr(offset, 4)), std::string(bytes.substr(offset + 8, length))});
        offset += 8 + length + (length & 1U);
        check(offset <= bytes.size());
    }
    return chunks;
}
std::string pack(const std::vector<Chunk>& chunks) {
    std::string body;
    for (const auto& chunk : chunks) {
        body += chunk.type + uint_le(static_cast<std::uint32_t>(chunk.payload.size())) + chunk.payload;
        if (chunk.payload.size() & 1U) body += '\0';
    }
    return "RIFF" + uint_le(static_cast<std::uint32_t>(body.size() + 4)) + "WEBP" + body;
}
std::string fixture(const std::filesystem::path& root, std::string_view name) {
    std::ifstream in(root / (std::string(name) + ".webp"), std::ios::binary);
    check(static_cast<bool>(in));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
HttpResponse response(const std::string& bytes, std::string type = "image/webp") {
    HttpResponse result;
    result.status = 200;
    result.content_type = std::move(type);
    result.body = bytes;
    return result;
}
constexpr auto artist = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
ArtistBackgroundCandidate candidate() {
    return {"123", "https://assets.fanart.tv/fanart/synthetic.webp", 1920, 1080};
}
}

int main(int argc, char** argv) {
    const auto cache_root = std::filesystem::temp_directory_path() /
        ("picdplayer-webp-" + std::to_string(::getpid()));
    std::error_code error;
    try {
        check(argc == 2);
        const std::filesystem::path fixtures = argv[1];
        unsigned cases = 0;
        const auto expect = [&](std::string_view name, const std::string& bytes, bool expected) {
            const auto image = load_artist_background_image(artist, candidate(), {}, {},
                [&](auto, auto, const auto&) { return response(bytes); });
            if (image.has_value() != expected ||
                (image && (image->mime_type != "image/webp" || image->bytes != bytes)))
                throw std::runtime_error("WebP case failed: " + std::string(name));
            ++cases;
        };
        const auto lossy = fixture(fixtures, "lossy-2x2");
        const auto lossless = fixture(fixtures, "lossless-2x2");
        const auto extended = fixture(fixtures, "extended-lossy-2x2");
        const auto extended_lossless = fixture(fixtures, "extended-lossless-2x2");
        const auto alpha = fixture(fixtures, "lossy-alpha-2x2");
        for (const auto* name : {"lossy-2x2", "lossless-2x2", "lossy-alpha-2x2", "lossless-alpha-2x2",
                 "extended-lossy-2x2", "extended-lossless-2x2", "4096x1", "1x4096", "4096x2048",
                 "2048x4096", "3840x2160"}) expect(name, fixture(fixtures, name), true);
        for (const auto* name : {"4097x1", "1x4097", "4096x2049"}) expect(name, fixture(fixtures, name), false);
        for (const auto& original : {lossy, lossless, extended, extended_lossless, alpha})
            for (std::size_t size = 0; size < original.size(); ++size) {
                auto truncated = original.substr(0, size);
                if (size >= 12) truncated.replace(4, 4, uint_le(static_cast<std::uint32_t>(size - 8)));
                expect("truncated inner chunk with consistent RIFF length", truncated, false);
            }
        for (const auto offset : {0U, 8U}) {
            auto invalid = lossy;
            invalid[offset] = 'x';
            expect("signature", invalid, false);
        }
        for (const auto length : {0U, 0xffffffffU}) {
            auto invalid = lossy;
            invalid.replace(4, 4, uint_le(length));
            expect("RIFF length", invalid, false);
            invalid = lossy;
            invalid.replace(16, 4, uint_le(length));
            expect("chunk length", invalid, false);
        }
        expect("empty RIFF", pack({}), false);
        expect("trailing data policy", lossy + 'x', false);
        auto vp8 = unpack(lossy);
        auto vp8l = unpack(lossless);
        auto ext = unpack(extended);
        auto alph = unpack(alpha);
        check(vp8.size() == 1 && vp8[0].type == "VP8 " && vp8l.size() == 1 && vp8l[0].type == "VP8L");
        check(ext.size() == 3 && ext[0].type == "VP8X" && ext[1].type == "VP8 " && ext[2].type == "XMP ");
        check(alph.size() == 3 && alph[1].type == "ALPH");
        expect("duplicate bitstream", pack({vp8[0], vp8[0]}), false);
        expect("mixed bitstreams", pack({vp8[0], vp8l[0]}), false);
        expect("duplicate VP8X", pack({ext[0], ext[0], ext[1], ext[2]}), false);
        expect("late VP8X", pack({vp8[0], ext[0], ext[2]}), false);
        expect("header only", pack({ext[0]}), false);
        expect("metadata flag without chunk", pack({ext[0], ext[1]}), false);
        expect("duplicate metadata", pack({ext[0], ext[1], ext[2], ext[2]}), false);
        expect("metadata may precede image", pack({ext[0], ext[2], ext[1]}), true);
        for (const unsigned bit : {1U, 2U, 64U, 128U}) {
            auto modified = ext;
            modified[0].payload[0] |= static_cast<char>(bit);
            expect("unsupported VP8X flag policy", pack(modified), false);
        }
        for (const auto byte : {1U, 2U, 3U}) {
            auto modified = ext;
            modified[0].payload[byte] = 1;
            expect("reserved VP8X policy", pack(modified), false);
        }
        for (const auto value : {0U, 4096U, 0xffffffU}) {
            auto modified = ext;
            modified[0].payload.replace(4, 3, uint_le(value, 3));
            expect("canvas mismatch or exceeded", pack(modified), false);
        }
        auto modified = ext;
        modified[0].payload[0] = 0;
        expect("chunk without metadata flag", pack(modified), false);
        modified = ext;
        modified[0].payload.pop_back();
        expect("short VP8X", pack(modified), false);
        modified[0].payload.append(2, '\0');
        expect("long VP8X", pack(modified), false);
        for (const auto& chunk : std::vector<Chunk>{{"ANIM", std::string(6, '\0')},
                 {"ANMF", std::string(16, '\0')}}) {
            expect("orphan animation policy", pack({ext[0], ext[1], chunk, ext[2]}), false);
        }
        for (const auto bit : {1U, 16U}) {
            auto modified_vp8 = vp8;
            modified_vp8[0].payload[0] ^= static_cast<char>(bit);
            expect("inter frame or invisible frame", pack(modified_vp8), false);
        }
        for (const auto index : {3U, 6U, 8U}) {
            auto modified_vp8 = vp8;
            modified_vp8[0].payload[index] = 0;
            expect("invalid VP8 signature or zero dimension", pack(modified_vp8), false);
        }
        for (const auto index : {7U, 9U}) {
            auto modified_vp8 = vp8;
            modified_vp8[0].payload[index] |= 64;
            expect("unsupported scale policy", pack(modified_vp8), false);
        }
        auto modified_vp8 = vp8;
        modified_vp8[0].payload[0] = static_cast<char>((static_cast<unsigned char>(modified_vp8[0].payload[0]) & ~14U) | 8U);
        expect("unsupported VP8 version", pack(modified_vp8), false);
        for (const auto partition : {0U, 0x7ffffU}) {
            modified_vp8 = vp8;
            modified_vp8[0].payload.replace(0, 3, uint_le((partition << 5) | 16U, 3));
            expect("invalid first partition", pack(modified_vp8), false);
        }
        auto modified_vp8l = vp8l;
        modified_vp8l[0].payload[0] = 0;
        expect("VP8L signature", pack(modified_vp8l), false);
        modified_vp8l = vp8l;
        modified_vp8l[0].payload[4] |= 32;
        expect("VP8L version", pack(modified_vp8l), false);
        modified_vp8l = vp8l;
        modified_vp8l[0].payload[4] ^= 16;
        expect("VP8L alpha hint not an admission condition", pack(modified_vp8l), true);
        expect("ALPH after image", pack({alph[0], alph[2], alph[1]}), false);
        expect("duplicate ALPH", pack({alph[0], alph[1], alph[1], alph[2]}), false);
        expect("ALPH with lossless", pack({alph[0], alph[1], vp8l[0]}), false);
        expect("ALPH flag without payload", pack({alph[0], alph[2]}), false);
        for (const unsigned header : {2U, 32U, 64U}) {
            auto modified_alpha = alph;
            modified_alpha[1].payload[0] = static_cast<char>(header);
            expect("unsupported ALPH header", pack(modified_alpha), false);
        }
        auto raw_alpha = alph;
        raw_alpha[1].payload = std::string(5, '\0');
        expect("raw alpha plane", pack(raw_alpha), true);
        raw_alpha[1].payload.pop_back();
        expect("short raw alpha plane", pack(raw_alpha), false);
        expect("unknown chunk ignored", pack({vp8[0], {"TEST", "x"}}), true);
        auto bad_padding = pack({vp8[0], {"TEST", "x"}});
        bad_padding.back() = 1;
        expect("RIFF padding", bad_padding, false);
        bad_padding.pop_back();
        bad_padding.replace(4, 4, uint_le(static_cast<std::uint32_t>(bad_padding.size() - 8)));
        expect("missing padding", bad_padding, false);
        for (const auto size : {artist_background_image_limit, artist_background_image_limit + 2}) {
            const auto padded = pack({vp8l[0], {"TEST", std::string(size - lossless.size() - 8, '\0')}});
            check(padded.size() == size);
            expect("encoded bound", padded, size == artist_background_image_limit);
        }

        std::filesystem::remove_all(cache_root, error);
        int calls = 0;
        ArtistImageHttpGet good = [&](auto, auto, const auto&) { ++calls; return response(lossless); };
        ArtistImageHttpGet offline = [&](auto, auto, const auto&) -> HttpResponse {
            ++calls;
            throw std::runtime_error("offline");
        };
        check(load_artist_background_image(artist, candidate(), cache_root, {}, good)->bytes == lossless);
        check(load_artist_background_image(artist, candidate(), cache_root, {}, offline)->bytes == lossless);
        check(calls == 1);
        std::filesystem::path cached;
        for (const auto& file : std::filesystem::directory_iterator(cache_root / "artist-background"))
            if (file.path().extension() == ".image") cached = file.path();
        check(!cached.empty());
        std::filesystem::last_write_time(cached,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
        check(load_artist_background_image(artist, candidate(), cache_root, {}, offline)->bytes == lossless);
        check(calls == 2);
        const auto oversized = fixture(fixtures, "4097x1");
        for (const bool stale : {false, true}) {
            check(load_artist_background_image(artist, candidate(), cache_root, {}, good)->bytes == lossless);
            {
                std::ofstream out(cached, std::ios::binary | std::ios::trunc);
                out.write(oversized.data(), static_cast<std::streamsize>(oversized.size()));
                check(static_cast<bool>(out));
            }
            if (stale) std::filesystem::last_write_time(cached,
                std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
            check(!load_artist_background_image(artist, candidate(), cache_root, {}, offline));
            check(!std::filesystem::exists(cached));
        }
        check(!load_artist_background_image(artist, candidate(), {}, [] { return true; }, good));
        bool cancelled = false;
        check(!load_artist_background_image(artist, candidate(), {}, [&] { return cancelled; },
            [&](auto, auto, const auto&) { cancelled = true; return response(lossless); }));
        check(!load_artist_background_image(artist, candidate(), {}, {},
            [&](auto, auto, const auto&) { return response(lossless, "image/png"); }));
        std::filesystem::remove_all(cache_root, error);
        std::cout << "PASS: " << cases << " WebP admission cases and cache/cancellation\n";
    } catch (const std::exception& failure) {
        std::filesystem::remove_all(cache_root, error);
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
