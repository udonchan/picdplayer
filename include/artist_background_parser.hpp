#pragma once

#include <string>
#include <string_view>
#include <vector>

struct ArtistBackgroundCandidate {
    std::string id;
    std::string url;
    unsigned width = 0;
    unsigned height = 0;
};

// Provider data remains internal to Enrichment; URLs must never enter the
// Presentation Model. The caller supplies the requested MusicBrainz Artist ID.
std::vector<ArtistBackgroundCandidate> parse_artist_backgrounds(
    std::string_view body, std::string_view artist_mbid);
