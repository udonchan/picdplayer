#pragma once
#include "metadata_model.hpp"
#include <string_view>

MetadataResult parse_musicbrainz_response(std::string_view json, std::string_view disc_id);
ArtworkInfo parse_cover_art_response(std::string_view json);
