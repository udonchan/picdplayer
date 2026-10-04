#pragma once
#include "metadata_worker.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

class MetadataSession {
public:
    MetadataRequest begin(const DiscToc& toc);
    // Restart after invalidation even when the refreshed TOC is unchanged.
    std::optional<MetadataRequest> begin_if_needed(const DiscToc& toc);
    void invalidate();
    bool apply(MetadataWorkerResult result);
    bool select_candidate(std::uint64_t generation, std::size_t index);
    bool decline_candidates(std::uint64_t generation);
    bool apply_artwork(std::uint64_t generation, const std::string& release_id, ArtworkInfo artwork);
    std::uint64_t generation() const { return generation_; }
    const MetadataResult& snapshot() const { return snapshot_; }
private:
    std::uint64_t generation_ = 0;
    std::optional<DiscToc> toc_;
    MetadataResult snapshot_;
};
