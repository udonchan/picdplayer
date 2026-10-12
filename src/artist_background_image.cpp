#include "artist_background_image.hpp"
#include "metadata_cache.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::size_t image_limit = 8 * 1024 * 1024;
constexpr MetadataCachePolicy image_policy{std::chrono::days(30), 64ULL * 1024 * 1024};

bool safe_name(std::string_view value) {
    return !value.empty() && value.size() <= 255 && value.front() != '.' &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.';
        });
}

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
    const std::string prefix = "https://assets.fanart.tv/fanart/music/" + std::string(mbid) +
                               "/artistbackground/";
    return candidate.url.starts_with(prefix) &&
           safe_name(std::string_view(candidate.url).substr(prefix.size()));
}

std::optional<std::string> image_mime(std::string_view bytes) {
    if (bytes.size() >= 4 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[bytes.size() - 2]) == 0xff &&
        static_cast<unsigned char>(bytes.back()) == 0xd9) return "image/jpeg";
    if (bytes.size() >= 20 && bytes.substr(0, 8) == "\x89PNG\r\n\x1a\n" &&
        bytes.substr(bytes.size() - 12, 8) == std::string_view("\x00\x00\x00\x00IEND", 8))
        return "image/png";
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
