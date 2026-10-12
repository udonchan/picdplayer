#pragma once

#include <string>

// Reads a configured fanart.tv key from a regular file. The path, contents,
// and validation errors must not be included in logs or API responses.
std::string load_artist_background_key(const std::string& path);
