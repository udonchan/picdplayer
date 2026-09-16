#pragma once
#include "disc_toc.hpp"
#include "metadata_model.hpp"
#include <filesystem>
#include <optional>
#include <string>

struct MetadataOptions {
    std::filesystem::path cache_directory;
    bool use_cache = true;
};

MetadataResult lookup_musicbrainz_disc(const DiscToc& toc, const MetadataOptions& options = {});
MetadataResult lookup_musicbrainz_id(const std::string& disc_id, const MetadataOptions& options = {});
void probe_metadata_device(const std::string& device, const MetadataOptions& options = {});
void probe_metadata_id(const std::string& disc_id, const MetadataOptions& options = {});
