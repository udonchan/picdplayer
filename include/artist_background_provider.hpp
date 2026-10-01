#pragma once

#include "artist_background_parser.hpp"
#include "http_client.hpp"
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

enum class ArtistBackgroundLookupStatus { disabled, available, unavailable, error };

struct ArtistBackgroundLookup {
    ArtistBackgroundLookupStatus status = ArtistBackgroundLookupStatus::disabled;
    std::vector<ArtistBackgroundCandidate> images;
};

using FanartHttpGet = std::function<HttpResponse(std::string_view url, std::string_view api_key,
                                                 std::size_t limit, const std::function<bool()>& cancelled)>;

// This provider adapter only obtains candidate metadata. It does not enable
// display, download images, or publish provider URLs to a View.
ArtistBackgroundLookup lookup_artist_backgrounds(
    std::string_view artist_mbid, std::string_view api_key,
    const std::function<bool()>& cancelled = {}, const FanartHttpGet& get = {},
    const std::filesystem::path& cache_directory = {});
