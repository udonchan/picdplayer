#pragma once

#include "disc_toc.hpp"
#include <string>

struct MusicBrainzDiscId {
    std::string id;
    std::string toc;
};

// Converts the validated player TOC to MusicBrainz offsets without accessing
// the drive. Throws invalid_argument for an inconsistent/unrepresentable TOC.
MusicBrainzDiscId calculate_musicbrainz_disc_id(const DiscToc& toc);
