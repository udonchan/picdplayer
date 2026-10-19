#pragma once

#include "read_policy.hpp"
#include <optional>
#include <string>

struct SettingsSnapshot {
    ReadPolicy read_policy{};
    bool artist_background_enabled = false;
};

// Versioned write to a bounded regular file. Caller owns application at a stopped boundary.
bool save_settings(const std::string& path, const SettingsSnapshot& snapshot,
                   std::size_t buffer_capacity_frames, std::string& error);
std::optional<SettingsSnapshot> load_settings(const std::string& path,
                                              std::size_t buffer_capacity_frames,
                                              std::string& error);
