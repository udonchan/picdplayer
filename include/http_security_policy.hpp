#pragma once

#include <string_view>

struct sockaddr;

// Cover Art Archive starts at coverartarchive.org and officially redirects
// artwork requests to Internet Archive hosts.
bool is_allowed_cover_art_url(std::string_view url);
bool is_allowed_cover_art_redirect(std::string_view location);
bool is_public_http_peer(const sockaddr* address);
