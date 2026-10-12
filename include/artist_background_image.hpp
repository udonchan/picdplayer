#pragma once

#include "artist_background_parser.hpp"
#include "http_client.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct ArtistBackgroundImage {
    std::string mime_type;
    std::string bytes;
};

using ArtistImageHttpGet = std::function<HttpResponse(
    std::string_view, std::size_t, const std::function<bool()>&)>;

// Downloads one validated provider candidate into the existing bounded file
// cache. This does not publish or display the image. Structural checks reject
// obvious truncation; full image decoding remains the View's responsibility.
std::optional<ArtistBackgroundImage> load_artist_background_image(
    std::string_view artist_mbid, const ArtistBackgroundCandidate& candidate,
    const std::filesystem::path& cache_directory,
    const std::function<bool()>& cancelled = {}, const ArtistImageHttpGet& get = {});
