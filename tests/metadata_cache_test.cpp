#include "metadata_cache.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("metadata cache test failed"); }
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "picdplayer-metadata-cache-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    try {
        const MetadataCachePolicy policy{.maximum_age = std::chrono::hours(1), .maximum_total_bytes = 10};
        const auto first = root / "metadata" / "first.json";
        write_metadata_cache(root, first, "fresh", policy);
        auto entry = read_metadata_cache(first, 10, policy);
        check(entry && entry->fresh && entry->body == "fresh");

        std::filesystem::last_write_time(first, std::filesystem::file_time_type::clock::now() - std::chrono::hours(2));
        entry = read_metadata_cache(first, 10, policy);
        check(entry && !entry->fresh && entry->body == "fresh");

        const auto second = root / "cover-art" / "second.json";
        write_metadata_cache(root, second, "123456", policy);
        check(!std::filesystem::exists(first));
        check(read_metadata_cache(second, 10, policy)->body == "123456");

        const auto temporary = root / "cover-art" / "interrupted.tmp";
        write_metadata_cache(root, temporary, "old", MetadataCachePolicy{.maximum_total_bytes = 20});
        write_metadata_cache(root, root / "metadata" / "third.json", "new", MetadataCachePolicy{.maximum_total_bytes = 20});
        check(!std::filesystem::exists(temporary));

        const auto oversized = root / "metadata" / "oversized.json";
        write_metadata_cache(root, oversized, "12345678901", policy);
        check(!std::filesystem::exists(oversized));

        write_metadata_cache(root, oversized, "0123456789", MetadataCachePolicy{.maximum_total_bytes = 20});
        check(!read_metadata_cache(oversized, 3));
        check(!std::filesystem::exists(oversized));
        std::filesystem::remove_all(root, ec);
        std::cout << "PASS: metadata cache freshness, capacity, and invalidation\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root, ec);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
