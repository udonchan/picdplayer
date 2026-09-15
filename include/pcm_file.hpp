#pragma once
#include <cstdint>
#include <span>
#include <string>

// Creates a new raw S16_LE file; refuses to overwrite existing paths.
// Removes the newly created incomplete file if writing fails.
void save_pcm_s16le(const std::string& path, std::span<const std::int16_t> pcm);
