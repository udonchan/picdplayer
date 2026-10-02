#pragma once
#include "disc_toc.hpp"
#include "http_client.hpp"
#include "metadata_model.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct MetadataOptions {
    using HttpGet = std::function<HttpResponse(std::string_view, std::size_t,
                                               const std::function<bool()>&, RedirectPolicy)>;
    std::filesystem::path cache_directory;
    bool use_cache = true;
    std::function<bool()> cancelled;
    // Test-only callers may supply deterministic responses. Production leaves
    // this empty and uses HttpClient.
    HttpGet http_get;
    // Runtime publishes metadata before the optional CAA lookup. Diagnostic
    // probe callers keep the combined result unless they opt out.
    bool include_artwork = true;
};

MetadataResult lookup_musicbrainz_disc(const DiscToc& toc, const MetadataOptions& options = {});
MetadataResult lookup_musicbrainz_id(const std::string& disc_id, const MetadataOptions& options = {});
ArtworkInfo lookup_cover_art_release(const std::string& release_id, const MetadataOptions& options = {});
void probe_metadata_device(const std::string& device, const MetadataOptions& options = {});
void probe_metadata_id(const std::string& disc_id, const MetadataOptions& options = {});
