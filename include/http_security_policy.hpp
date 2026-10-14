#pragma once

#include <string_view>

struct sockaddr;

// Cover Art Archive starts at coverartarchive.org and officially redirects
// artwork requests to Internet Archive hosts.
bool is_allowed_cover_art_url(std::string_view url);
bool is_allowed_cover_art_redirect(std::string_view location);
bool is_public_http_peer(const sockaddr* address);

// The API's artistbackground array owns artist/type identity. CDN paths are
// opaque: accept observed flat /fanart/ assets and the documented music tree,
// without requiring the MBID or JSON image type in directory names.
inline bool is_allowed_artist_background_url(std::string_view url) {
    constexpr std::string_view prefix = "https://assets.fanart.tv/fanart/";
    if (url.size() > 2048 || !url.starts_with(prefix)) return false;
    auto path = url.substr(prefix.size());
    if (path.find('/') != std::string_view::npos && !path.starts_with("music/")) return false;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto segment = path.substr(0, slash);
        if (segment.empty() || segment.size() > 255 || segment.front() == '.') return false;
        for (const unsigned char c : segment) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
        }
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash + 1);
    }
    return false;
}
