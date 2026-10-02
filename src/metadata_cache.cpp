#include "metadata_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <vector>

namespace {
struct CacheFile {
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified;
};

std::vector<CacheFile> cache_files(const std::filesystem::path& root) {
    std::vector<CacheFile> files;
    for (const auto* name : {"metadata", "cover-art", "artist-background"}) {
        std::error_code ec;
        std::filesystem::recursive_directory_iterator iterator(
            root / name, std::filesystem::directory_options::skip_permission_denied, ec);
        for (const auto end = std::filesystem::recursive_directory_iterator(); iterator != end;
             iterator.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!iterator->is_regular_file(ec) || ec) { ec.clear(); continue; }
            if (iterator->path().extension() == ".tmp") {
                std::filesystem::remove(iterator->path(), ec);
                ec.clear();
                continue;
            }
            const auto size = iterator->file_size(ec);
            if (ec) { ec.clear(); continue; }
            const auto modified = iterator->last_write_time(ec);
            if (ec) { ec.clear(); continue; }
            files.push_back({iterator->path(), size, modified});
        }
    }
    return files;
}

void make_space(const std::filesystem::path& root, const std::filesystem::path& replacement,
                std::size_t incoming_size, std::uintmax_t maximum_total_bytes) {
    auto files = cache_files(root);
    std::uintmax_t total = 0;
    for (const auto& file : files) {
        if (file.path != replacement) total += file.size;
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.modified < right.modified;
    });
    std::error_code ec;
    for (const auto& file : files) {
        if (total + incoming_size <= maximum_total_bytes) break;
        if (file.path == replacement) continue;
        if (std::filesystem::remove(file.path, ec)) total -= file.size;
        ec.clear();
    }
}
}

std::optional<MetadataCacheEntry> read_metadata_cache(const std::filesystem::path& path,
                                                      std::size_t maximum_bytes, MetadataCachePolicy policy) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return std::nullopt;
    if (size > maximum_bytes) {
        invalidate_metadata_cache(path);
        return std::nullopt;
    }
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    MetadataCacheEntry entry{std::string(std::istreambuf_iterator<char>(input), {}), false};
    if (!input.good() && !input.eof()) return std::nullopt;
    entry.fresh = std::filesystem::file_time_type::clock::now() - modified <= policy.maximum_age;
    return entry;
}

void invalidate_metadata_cache(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void write_metadata_cache(const std::filesystem::path& root, const std::filesystem::path& path,
                          std::string_view body, MetadataCachePolicy policy) {
    if (body.size() > policy.maximum_total_bytes) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;
    make_space(root, path, body.size(), policy.maximum_total_bytes);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!output) return;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) std::filesystem::remove(temporary, ec);
}
