#pragma once

#include "read_policy.hpp"
#include <optional>
#include <string>

// A small, versioned user setting. The caller owns application at a stopped boundary.
std::optional<ReadPolicy> load_saved_read_policy(const std::string& path,
                                                  std::size_t buffer_capacity_frames,
                                                  std::string& error);
bool save_read_policy(const std::string& path, const ReadPolicy& policy,
                      std::size_t buffer_capacity_frames, std::string& error);
