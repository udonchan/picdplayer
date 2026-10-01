#pragma once

#include "metadata_model.hpp"
#include "disc_toc.hpp"
#include <memory>
#include <optional>
#include <string>

struct EnrichmentArtworkAsset {
    std::string mime_type;
    std::string bytes;
};

// Optional external metadata is isolated behind this service.  Playback only
// supplies a validated TOC and never waits for a result.
class EnrichmentService {
public:
    class Implementation;
    EnrichmentService(bool enabled, std::string cache_directory,
                      std::string artist_background_key = {});
    ~EnrichmentService();
    EnrichmentService(const EnrichmentService&) = delete;
    EnrichmentService& operator=(const EnrichmentService&) = delete;

    void begin_if_needed(const DiscToc& toc);
    void invalidate();
    void poll();
    const MetadataResult& snapshot() const;
    std::uint64_t generation() const;
    bool select_candidate(std::uint64_t generation, std::size_t index);
    bool has_cover_asset() const;
    std::optional<EnrichmentArtworkAsset> cover_asset() const;

private:
    std::unique_ptr<Implementation> implementation_;
};
