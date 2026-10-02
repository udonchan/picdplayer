#include "artist_background_provider.hpp"
#include "metadata_cache.hpp"

#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <optional>

namespace {
constexpr std::size_t response_limit = 512 * 1024;
constexpr MetadataCachePolicy candidate_policy{std::chrono::days(7), 64ULL * 1024 * 1024};
constexpr MetadataCachePolicy missing_policy{std::chrono::days(1), 64ULL * 1024 * 1024};

bool valid_mbid(std::string_view value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
}

ArtistBackgroundLookup lookup_artist_backgrounds(
    std::string_view artist_mbid, std::string_view api_key,
    const std::function<bool()>& cancelled, const FanartHttpGet& get,
    const std::filesystem::path& cache_directory) {
    if (api_key.empty()) return {};
    if (!valid_mbid(artist_mbid)) return {ArtistBackgroundLookupStatus::error, {}};
    if (api_key.size() > 256 || std::any_of(api_key.begin(), api_key.end(), [](unsigned char c) {
            return c <= 0x20 || c >= 0x7f;
        })) return {ArtistBackgroundLookupStatus::error, {}};
    if (cancelled && cancelled()) return {ArtistBackgroundLookupStatus::error, {}};
    const auto cache_path = cache_directory.empty() ? std::filesystem::path{} :
        cache_directory / "artist-background" / (std::string(artist_mbid) + ".json");
    const auto missing_path = cache_directory.empty() ? std::filesystem::path{} :
        cache_directory / "artist-background" / (std::string(artist_mbid) + ".missing");
    std::optional<MetadataCacheEntry> cached;
    if (!cache_directory.empty()) {
        const auto missing = read_metadata_cache(missing_path, 0, missing_policy);
        if (missing && missing->fresh) return {ArtistBackgroundLookupStatus::unavailable, {}};
        cached = read_metadata_cache(cache_path, response_limit, candidate_policy);
        if (cached) {
            try {
                auto images = parse_artist_backgrounds(cached->body, artist_mbid);
                if (cached->fresh) return {images.empty() ? ArtistBackgroundLookupStatus::unavailable :
                    ArtistBackgroundLookupStatus::available, std::move(images)};
            } catch (const std::exception&) {
                invalidate_metadata_cache(cache_path);
                cached.reset();
            }
        }
    }
    try {
        const std::string url = "https://webservice.fanart.tv/v3.2/music/" + std::string(artist_mbid);
        const auto response = get ? get(url, api_key, response_limit, cancelled) :
            HttpClient{}.get(url, response_limit, cancelled, RedirectPolicy::reject, api_key);
        if (cancelled && cancelled()) return {ArtistBackgroundLookupStatus::error, {}};
        if (response.status == 404) {
            if (!cache_directory.empty() && !(cancelled && cancelled())) {
                invalidate_metadata_cache(cache_path);
                write_metadata_cache(cache_directory, missing_path, "", missing_policy);
            }
            return {ArtistBackgroundLookupStatus::unavailable, {}};
        }
        if (response.status != 200 || !response.content_type.starts_with("application/json"))
            throw std::runtime_error("artist background provider response is unavailable");
        auto images = parse_artist_backgrounds(response.body, artist_mbid);
        if (!cache_directory.empty() && !(cancelled && cancelled())) {
            invalidate_metadata_cache(missing_path);
            write_metadata_cache(cache_directory, cache_path, response.body, candidate_policy);
        }
        return {images.empty() ? ArtistBackgroundLookupStatus::unavailable :
                   ArtistBackgroundLookupStatus::available, std::move(images)};
    } catch (const std::exception&) {
        if (cached && !(cancelled && cancelled())) {
            try {
                auto images = parse_artist_backgrounds(cached->body, artist_mbid);
                return {images.empty() ? ArtistBackgroundLookupStatus::unavailable :
                    ArtistBackgroundLookupStatus::available, std::move(images)};
            } catch (const std::exception&) { invalidate_metadata_cache(cache_path); }
        }
        return {ArtistBackgroundLookupStatus::error, {}};
    }
}
