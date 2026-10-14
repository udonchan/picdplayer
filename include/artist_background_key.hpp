#pragma once

#include <string>

// Reads a configured fanart.tv key from a regular file. The path, contents,
// and validation errors must not be included in logs or API responses.
std::string load_artist_background_key(const std::string& path);

// An explicitly selected file takes precedence over the local-development
// FANART_TV_API_KEY environment variable, including when the file is invalid.
std::string resolve_artist_background_key(const std::string& path);
