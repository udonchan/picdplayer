#include "artist_background_image.hpp"

#include <algorithm>
#include <array>
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
#include <unistd.h>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("JPEG invariant failed"); }
std::string be16(unsigned value) {
    return std::string{static_cast<char>(value >> 8), static_cast<char>(value & 255)};
}
unsigned read16(std::string_view bytes, std::size_t offset) {
    return (static_cast<unsigned char>(bytes.at(offset)) << 8) |
        static_cast<unsigned char>(bytes.at(offset + 1));
}
std::string marker(unsigned value) { return std::string("\xff", 1) + static_cast<char>(value); }
std::string segment(unsigned type, std::string_view payload) {
    check(payload.size() <= 65533);
    return marker(type) + be16(static_cast<unsigned>(payload.size() + 2)) + std::string(payload);
}
// Used only on the known codec fixtures, whose header payloads do not contain
// the marker pairs selected by these mutations. This is not a JPEG parser.
std::size_t position(const std::string& bytes, unsigned type, std::size_t from = 0) {
    const auto found = bytes.find(marker(type), from);
    check(found != std::string::npos);
    return found;
}
std::string payload(const std::string& bytes, unsigned type) {
    const auto p = position(bytes, type);
    const auto length = read16(bytes, p + 2);
    check(length >= 2 && length <= bytes.size() - p - 2);
    return bytes.substr(p + 4, length - 2);
}
std::string replace(const std::string& bytes, unsigned type, const std::string& data) {
    const auto p = position(bytes, type);
    return bytes.substr(0, p) + segment(type, data) + bytes.substr(p + 2 + read16(bytes, p + 2));
}
std::string without(std::string bytes, unsigned type) {
    for (auto p = bytes.find(marker(type)); p != std::string::npos; p = bytes.find(marker(type)))
        bytes.erase(p, 2 + read16(bytes, p + 2));
    return bytes;
}
std::string fixture(const std::filesystem::path& root, std::string_view name) {
    std::ifstream in(root / (std::string(name) + ".jpg"), std::ios::binary);
    check(static_cast<bool>(in));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
HttpResponse response(const std::string& bytes, std::string type = "image/jpeg") {
    HttpResponse result;
    result.status = 200;
    result.content_type = std::move(type);
    result.body = bytes;
    return result;
}
constexpr auto artist = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
ArtistBackgroundCandidate candidate() {
    return {"123", "https://assets.fanart.tv/fanart/synthetic.jpg", 1920, 1080};
}
}

int main(int argc, char** argv) {
    const auto root = std::filesystem::temp_directory_path() /
        ("picdplayer-jpeg-" + std::to_string(::getpid()));
    std::error_code error;
    try {
        check(argc == 2);
        const std::filesystem::path fixtures = argv[1];
        unsigned cases = 0;
        const auto expect = [&](std::string_view label, const std::string& bytes, bool expected) {
            const auto image = load_artist_background_image(artist, candidate(), {}, {},
                [&](auto, auto, const auto&) { return response(bytes); });
            if (image.has_value() != expected ||
                (image && (image->mime_type != "image/jpeg" || image->bytes != bytes)))
                throw std::runtime_error("JPEG case failed: " + std::string(label));
            ++cases;
        };
        const auto baseline = fixture(fixtures, "baseline-2x2");
        const auto sequential = fixture(fixtures, "sequential-2x2");
        const auto progressive = fixture(fixtures, "progressive-2x2");
        const auto restarted = fixture(fixtures, "restart-32x32");
        const auto stuffed = fixture(fixtures, "stuffed-64x64");
        for (const auto* name : {"baseline-2x2", "sequential-2x2", "progressive-2x2", "grayscale-2x2",
                 "cmyk-2x2", "restart-32x32", "stuffed-64x64", "4096x1", "1x4096",
                 "4096x2048", "2048x4096", "3840x2160"}) expect(name, fixture(fixtures, name), true);
        for (const auto* name : {"4097x1", "1x4097", "4096x2049", "wide-quantization-2x2"})
            expect(name, fixture(fixtures, name), false);
        for (const auto& original : {baseline, sequential, progressive, restarted, stuffed})
            for (std::size_t size = 0; size < original.size(); ++size)
                expect("truncated marker or scan", original.substr(0, size), false);
        const auto frame = payload(baseline, 0xc0);
        const auto frame_segment = segment(0xc0, frame);
        const auto frame_position = position(baseline, 0xc0);
        const auto before_end = baseline.substr(0, baseline.size() - 2);
        const auto end = marker(0xd9);
        expect("no frame/scan", marker(0xd8) + end, false);
        expect("duplicate SOF", baseline.substr(0, frame_position) + frame_segment + baseline.substr(frame_position), false);
        expect("late SOF", before_end + frame_segment + end, false);
        auto conflicting = frame;
        conflicting.replace(3, 2, be16(3));
        expect("late conflicting dimensions", before_end + segment(0xc0, conflicting) + end, false);
        expect("DNL product policy", before_end + segment(0xdc, be16(2)) + end, false);
        for (const auto type : {0xdeU, 0xdfU, 0xccU})
            expect("hierarchical/arithmetic policy", marker(0xd8) + segment(type, frame) + baseline.substr(2), false);
        for (const auto type : {0xc3U, 0xc5U, 0xc9U}) {
            auto unsupported = baseline;
            unsupported[frame_position + 1] = static_cast<char>(type);
            expect("unsupported SOF", unsupported, false);
        }
        for (const auto field : {0U, 1U, 3U}) {
            auto changed = frame;
            if (field == 0) changed[0] = 12;
            else changed.replace(field, 2, be16(0));
            expect("precision or zero dimension", replace(baseline, 0xc0, changed), false);
        }
        for (const auto width : {4097U, 65535U}) {
            auto changed = frame;
            changed.replace(3, 2, be16(width));
            expect("structural width field", replace(baseline, 0xc0, changed), false);
        }
        auto changed = frame;
        changed[7] = 0;
        expect("zero sampling", replace(baseline, 0xc0, changed), false);
        changed[7] = 0x51;
        expect("sampling exceeded", replace(baseline, 0xc0, changed), false);
        changed = frame;
        changed[9] = changed[6];
        expect("duplicate frame component", replace(baseline, 0xc0, changed), false);
        changed = frame;
        changed[8] = 4;
        expect("quantizer selector", replace(baseline, 0xc0, changed), false);
        expect("short SOF", replace(baseline, 0xc0, frame.substr(0, frame.size() - 1)), false);
        expect("missing quantization tables", without(baseline, 0xdb), false);
        expect("missing Huffman tables", without(baseline, 0xc4), false);
        auto quant = payload(baseline, 0xdb);
        quant[1] = 0;
        expect("zero quantizer", replace(baseline, 0xdb, quant), false);
        quant = payload(baseline, 0xdb);
        quant[0] = 4;
        expect("quantizer destination", replace(baseline, 0xdb, quant), false);
        expect("truncated DQT", replace(baseline, 0xdb, payload(baseline, 0xdb).substr(0, 64)), false);
        auto huffman = payload(baseline, 0xc4);
        huffman[0] = 0x20;
        expect("Huffman class", replace(baseline, 0xc4, huffman), false);
        huffman = payload(baseline, 0xc4);
        huffman[1] = static_cast<char>(255);
        expect("oversubscribed Huffman tree", replace(baseline, 0xc4, huffman), false);
        expect("empty DHT", replace(baseline, 0xc4, {}), false);
        const auto scan = payload(baseline, 0xda);
        for (const auto field : {1U, 2U, 7U, 8U, 9U}) {
            auto changed_scan = scan;
            changed_scan[field] = field == 1 ? 99 : field == 2 ? 0x33 : 1;
            expect("invalid sequential scan", replace(baseline, 0xda, changed_scan), false);
        }
        auto duplicate = scan;
        duplicate[3] = duplicate[1];
        expect("duplicate scan selector", replace(baseline, 0xda, duplicate), false);
        expect("missing entropy", baseline.substr(0, position(baseline, 0xda) + 2 + read16(baseline, position(baseline, 0xda) + 2)) + end, false);
        const auto first_scan = position(progressive, 0xda);
        const auto next_table = position(progressive, 0xc4, first_scan + 2);
        expect("duplicate progressive initial scan", progressive.substr(0, progressive.size() - 2) +
            progressive.substr(first_scan, next_table - first_scan) + end, false);
        auto progressive_scan = payload(progressive, 0xda);
        progressive_scan.back() = 0x31;
        expect("invalid refinement step", replace(progressive, 0xda, progressive_scan), false);
        auto changed_quant = payload(progressive, 0xdb);
        check(changed_quant.size() == 65 && static_cast<unsigned char>(changed_quant[0]) <= 3);
        const auto identical_quant = segment(0xdb, changed_quant);
        expect("identical used progressive quantizer", progressive.substr(0, next_table) +
            identical_quant + progressive.substr(next_table), true);
        changed_quant[1] = changed_quant[1] == 1 ? 2 : 1;
        expect("changed used progressive quantizer", progressive.substr(0, next_table) +
            segment(0xdb, changed_quant) + progressive.substr(next_table), false);
        const auto scan_end = [&](std::size_t start) {
            auto p = start + 2 + read16(progressive, start + 2);
            while (p + 1 < progressive.size()) {
                if (static_cast<unsigned char>(progressive[p]) != 255) { ++p; continue; }
                const auto next = static_cast<unsigned char>(progressive[p + 1]);
                if (next == 0 || (next >= 0xd0 && next <= 0xd7)) { p += 2; continue; }
                return p;
            }
            throw std::runtime_error("fixture scan terminator missing");
        };
        const auto first_ac = position(progressive, 0xda, next_table);
        const auto first_ac_end = scan_end(first_ac);
        expect("AC before initial DC", progressive.substr(0, first_scan) +
            progressive.substr(next_table, first_ac_end - next_table) +
            progressive.substr(first_scan, next_table - first_scan) + end, false);
        bool checked_history = false;
        for (auto p = progressive.find(marker(0xda)); p != std::string::npos;
             p = progressive.find(marker(0xda), p + 2)) {
            auto scan_header = progressive.substr(p + 4, read16(progressive, p + 2) - 2);
            const auto selectors = static_cast<unsigned char>(scan_header[0]);
            if (scan_header[1 + 2 * selectors] != 0 ||
                (static_cast<unsigned char>(scan_header.back()) >> 4) == 0) continue;
            const auto data_start = p + 2 + read16(progressive, p + 2);
            scan_header.back() = 0x21; // Locally valid Ah=2, Al=1, but final DC history is 0.
            expect("refinement with wrong coefficient history", progressive.substr(0, progressive.size() - 2) +
                segment(0xda, scan_header) + progressive.substr(data_start, scan_end(p) - data_start) + end, false);
            checked_history = true;
            break;
        }
        check(checked_history);
        changed = frame;
        changed[7] = 0x33; // 9 Y blocks + 1 Cb + 1 Cr; all individual factors remain legal.
        expect("interleaved eleven blocks", replace(baseline, 0xc0, changed), false);
        expect("unchanged progressive control", progressive, true);
        expect("unchanged baseline control", baseline, true);
        std::array<bool, 256> tables{};
        bool redefined = false;
        for (auto p = progressive.find(marker(0xc4)); p != std::string::npos; p = progressive.find(marker(0xc4), p + 2)) {
            const auto table = static_cast<unsigned char>(progressive.at(p + 4));
            redefined |= tables[table];
            tables[table] = true;
        }
        check(redefined); // The accepted codec fixture really redefines DHT destinations.
        const auto stuffed_position = stuffed.find(std::string("\xff\x00", 2));
        check(stuffed_position != std::string::npos);
        auto unstuffed = stuffed;
        unstuffed[stuffed_position + 1] = 1;
        expect("unsupported unstuffed marker", unstuffed, false);
        auto wrong_restart = restarted;
        wrong_restart[position(restarted, 0xd0) + 1] = static_cast<char>(0xd1);
        expect("restart sequence", wrong_restart, false);
        expect("restart disabled", replace(restarted, 0xdd, be16(0)), false);
        expect("restart outside scan", marker(0xd8) + marker(0xd0) + baseline.substr(2), false);
        expect("marker fill before SOF", baseline.substr(0, frame_position) + '\xff' + baseline.substr(frame_position), true);
        expect("marker fill before EOI", before_end + '\xff' + end, true);
        expect("stuffing outside entropy", marker(0xd8) + std::string("\xff\x00", 2) + baseline.substr(2), false);
        expect("markers inside APP are opaque", marker(0xd8) + segment(0xef,
            std::string("\xff\xd8\xff\xc0\xff\xdc\xff\x00", 8)) + baseline.substr(2), true);
        expect("duplicate SOI", marker(0xd8) + baseline, false);
        expect("trailing bytes policy", baseline + 'x', false);
        expect("two images policy", baseline + baseline, false);
        for (const auto length : {0U, 1U, 65535U}) {
            auto malformed = baseline;
            malformed.replace(frame_position + 2, 2, be16(length));
            expect("segment length", malformed, false);
        }
        for (const auto size : {artist_background_image_limit, artist_background_image_limit + 1}) {
            std::string padded = marker(0xd8);
            std::size_t remaining = size - baseline.size();
            while (remaining != 0) {
                auto length = std::min<std::size_t>(remaining, 65537);
                if (remaining > length && remaining - length < 4) length -= 4 - (remaining - length);
                check(length >= 4);
                padded += segment(0xef, std::string(length - 4, 'x'));
                remaining -= length;
            }
            padded += baseline.substr(2);
            check(padded.size() == size);
            expect("encoded bound", padded, size == artist_background_image_limit);
        }

        std::filesystem::remove_all(root, error);
        int calls = 0;
        ArtistImageHttpGet good = [&](auto, auto, const auto&) { ++calls; return response(baseline); };
        ArtistImageHttpGet offline = [&](auto, auto, const auto&) -> HttpResponse {
            ++calls;
            throw std::runtime_error("offline");
        };
        check(load_artist_background_image(artist, candidate(), root, {}, good)->bytes == baseline);
        check(load_artist_background_image(artist, candidate(), root, {}, offline)->bytes == baseline);
        check(calls == 1);
        std::filesystem::path cached;
        for (const auto& file : std::filesystem::directory_iterator(root / "artist-background"))
            if (file.path().extension() == ".image") cached = file.path();
        check(!cached.empty());
        std::filesystem::last_write_time(cached,
            std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
        check(load_artist_background_image(artist, candidate(), root, {}, offline)->bytes == baseline);
        check(calls == 2);
        const auto over = fixture(fixtures, "4097x1");
        for (const bool stale : {false, true}) {
            check(load_artist_background_image(artist, candidate(), root, {}, good)->bytes == baseline);
            { std::ofstream out(cached, std::ios::binary | std::ios::trunc); out.write(over.data(), static_cast<std::streamsize>(over.size())); check(static_cast<bool>(out)); }
            if (stale) std::filesystem::last_write_time(cached,
                std::filesystem::file_time_type::clock::now() - std::chrono::days(31));
            check(!load_artist_background_image(artist, candidate(), root, {}, offline));
            check(!std::filesystem::exists(cached));
        }
        check(!load_artist_background_image(artist, candidate(), {}, [] { return true; }, good));
        bool cancelled = false;
        check(!load_artist_background_image(artist, candidate(), {}, [&] { return cancelled; },
            [&](auto, auto, const auto&) { cancelled = true; return response(baseline); }));
        check(!load_artist_background_image(artist, candidate(), {}, {},
            [&](auto, auto, const auto&) { return response(baseline, "image/png"); }));
        std::filesystem::remove_all(root, error);
        std::cout << "PASS: " << cases << " JPEG admission cases and cache/cancellation\n";
    } catch (const std::exception& failure) {
        std::filesystem::remove_all(root, error);
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
