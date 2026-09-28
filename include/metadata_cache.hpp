#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct MetadataCacheEntry {
    std::string body;
    bool fresh = false;
};

struct MetadataCachePolicy {
    std::chrono::seconds maximum_age = std::chrono::days(30);
    std::uintmax_t maximum_total_bytes = 64ULL * 1024 * 1024;
};

// Raw metadata and artwork cache.  A stale entry remains available only as an
// offline fallback; callers must prefer a fresh network response.
std::optional<MetadataCacheEntry> read_metadata_cache(const std::filesystem::path& path,
                                                      std::size_t maximum_bytes,
                                                      MetadataCachePolicy policy = {});
void invalidate_metadata_cache(const std::filesystem::path& path);
void write_metadata_cache(const std::filesystem::path& root, const std::filesystem::path& path,
                          std::string_view body, MetadataCachePolicy policy = {});
