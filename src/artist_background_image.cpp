#include "artist_background_image.hpp"
#include "metadata_cache.hpp"
#include "http_security_policy.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::size_t image_limit = artist_background_image_limit;
constexpr MetadataCachePolicy image_policy{std::chrono::days(30), 64ULL * 1024 * 1024};

bool allowed_candidate(std::string_view mbid, const ArtistBackgroundCandidate& candidate) {
    if (mbid.size() != 36 || candidate.width == 0 || candidate.height == 0 ||
        candidate.id.empty() || candidate.id.size() > 64 ||
        !std::all_of(candidate.id.begin(), candidate.id.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) return false;
    for (std::size_t i = 0; i < mbid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (mbid[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(mbid[i]))) return false;
    }
    return is_allowed_artist_background_url(candidate.url);
}

std::uint32_t png_u32(std::string_view bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    return value;
}

std::uint32_t png_crc(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

// Admission inspects chunks, not the compressed pixel or ancillary payloads.
bool admitted_png(std::string_view bytes) {
    if (bytes.size() > image_limit || bytes.substr(0, 8) != "\x89PNG\r\n\x1a\n") return false;
    std::size_t offset = 8;
    unsigned depth = 0, color = 0;
    bool header = false, palette = false, data = false, data_ended = false, nonempty_data = false;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 12) return false;
        const auto length = png_u32(bytes, offset);
        if (length > 0x7fffffffU || length > bytes.size() - offset - 12) return false;
        const auto type = bytes.substr(offset + 4, 4);
        for (const unsigned char letter : type)
            if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))) return false;
        if (type[2] < 'A' || type[2] > 'Z') return false;
        const auto payload = bytes.substr(offset + 8, length);
        if (png_crc(bytes.substr(offset + 4, length + 4)) != png_u32(bytes, offset + 8 + length))
            return false;
        offset += length + 12;
        if (!header && type != "IHDR") return false;
        if (type == "IHDR") {
            if (header || length != 13) return false;
            const auto width = png_u32(payload, 0), height = png_u32(payload, 4);
            if (width == 0 || height == 0 || width > 4096 || height > 4096 ||
                static_cast<std::uint64_t>(width) * height > 8388608) return false;
            depth = static_cast<unsigned char>(payload[8]);
            color = static_cast<unsigned char>(payload[9]);
            const bool legal_depth = (color == 0 || color == 3) ?
                (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                ((color == 2 || color == 4 || color == 6) && depth == 8);
            if (!legal_depth || payload[10] != 0 || payload[11] != 0 ||
                (payload[12] != 0 && payload[12] != 1)) return false;
            header = true;
        } else if (type == "IDAT") {
            if (data_ended || (color == 3 && !palette)) return false;
            data = true;
            nonempty_data |= !payload.empty();
        } else {
            if (data) data_ended = true;
            if (type == "PLTE") {
                if (palette || data || color == 0 || color == 4 || length == 0 ||
                    length > 768 || length % 3 != 0 ||
                    (color == 3 && length / 3 > (1U << depth))) return false;
                palette = true;
            } else if (type == "IEND") {
                return length == 0 && data && nonempty_data && offset == bytes.size();
            } else if (type == "acTL" || type == "fcTL" || type == "fdAT" ||
                       (type[0] >= 'A' && type[0] <= 'Z')) {
                return false;
            }
        }
    }
    return false;
}

std::optional<std::string> image_mime(std::string_view bytes) {
    if (bytes.size() >= 4 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[bytes.size() - 2]) == 0xff &&
        static_cast<unsigned char>(bytes.back()) == 0xd9) return "image/jpeg";
    if (admitted_png(bytes)) return "image/png";
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") {
        const auto encoded = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[4])) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[5])) << 8) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[6])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[7])) << 24);
        if (encoded == bytes.size() - 8) return "image/webp";
    }
    return std::nullopt;
}

std::string url_hash(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}
bool matching_content_type(std::string_view content_type, std::string_view mime) {
    return content_type.starts_with(mime) &&
        (content_type.size() == mime.size() || content_type[mime.size()] == ';');
}
}

std::optional<ArtistBackgroundImage> load_artist_background_image(
    std::string_view artist_mbid, const ArtistBackgroundCandidate& candidate,
    const std::filesystem::path& cache_directory,
    const std::function<bool()>& cancelled, const ArtistImageHttpGet& get) {
    if (!allowed_candidate(artist_mbid, candidate) || (cancelled && cancelled())) return std::nullopt;
    const auto path = cache_directory.empty() ? std::filesystem::path{} :
        cache_directory / "artist-background" /
        (std::string(artist_mbid) + "-" + candidate.id + "-" + url_hash(candidate.url) + ".image");
    std::optional<MetadataCacheEntry> cached;
    if (!cache_directory.empty()) {
        cached = read_metadata_cache(path, image_limit, image_policy);
        if (cached) {
            const auto mime = image_mime(cached->body);
            if (!mime) {
                invalidate_metadata_cache(path);
                cached.reset();
            } else if (cached->fresh) return ArtistBackgroundImage{*mime, std::move(cached->body)};
        }
    }
    try {
        auto response = get ? get(candidate.url, image_limit, cancelled) :
            HttpClient{}.get(candidate.url, image_limit, cancelled, RedirectPolicy::fanart_asset);
        if (cancelled && cancelled()) return std::nullopt;
        if (response.status == 403 || response.status == 404 || response.status == 410) {
            if (!cache_directory.empty()) invalidate_metadata_cache(path);
            return std::nullopt;
        }
        if (response.status != 200 || response.body.empty() || response.body.size() > image_limit)
            throw std::runtime_error("artist background image is unavailable");
        const auto mime = image_mime(response.body);
        if (!mime || !matching_content_type(response.content_type, *mime)) {
            if (!cache_directory.empty()) invalidate_metadata_cache(path);
            return std::nullopt;
        }
        if (!cache_directory.empty())
            write_metadata_cache(cache_directory, path, response.body, image_policy);
        return ArtistBackgroundImage{*mime, std::move(response.body)};
    } catch (const std::exception&) {
        if (cached && !(cancelled && cancelled())) {
            if (const auto mime = image_mime(cached->body))
                return ArtistBackgroundImage{*mime, std::move(cached->body)};
        }
        return std::nullopt;
    }
}
