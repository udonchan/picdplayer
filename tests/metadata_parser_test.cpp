#include "metadata_parser.hpp"
#include <iostream>
#include <stdexcept>

namespace { void check(bool value) { if (!value) throw std::runtime_error("metadata parser test failed"); } }
int main() {
    try {
        const std::string one = R"({"releases":[{"id":"rel-1","title":"Album","country":"JP","date":"2020","artist-credit":[{"name":"Alpha","joinphrase":" & "},{"name":"Beta"}],"release-group":{"id":"group-1"},"media":[{"position":1,"discs":[{"id":"disc"}],"tracks":[{"position":1,"title":"Song","length":123000,"artist-credit":[{"name":"Singer"}],"recording":{"id":"rec-1"}}]}]}]})";
        auto result = parse_musicbrainz_response(one, "disc");
        check(result.status == MetadataStatus::available && result.selected == 0 && result.candidates.size() == 1);
        const auto& disc = result.candidates[0].metadata;
        check(disc.album_title == "Album" && disc.album_artist == "Alpha & Beta" && disc.release_group_id == "group-1");
        check(disc.tracks.size() == 1 && disc.tracks[0].recording_id == "rec-1" && disc.tracks[0].artist == "Singer");
        check(disc.artist_identity.status == ArtistIdentityStatus::unavailable);
        const std::string artist_id = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
        const auto with_credit = [](std::string_view credit) {
            return std::string("{\"releases\":[{\"artist-credit\":") + std::string(credit) +
                ",\"media\":[{\"discs\":[{\"id\":\"disc\"}],\"tracks\":[]}]}]}";
        };
        auto identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"Queen","artist":{"id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3"}}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::available);
        check(identity.candidates[0].metadata.artist_identity.mbid == artist_id);
        identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"One","joinphrase":" & ","artist":{"id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3"}},{"name":"Two","artist":{"id":"89ad4ac3-39f7-470e-963a-56509c546378"}}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::ambiguous);
        identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"Queen","joinphrase":" with ","artist":{"id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3"}},{"name":"Queen","artist":{"id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3"}}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::available);
        identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"One","artist":{"id":"0383dadf-2a4e-4d10-a46a-e9e041da8eb3"}},{"name":"Two"}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::unavailable);
        identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"Various Artists","artist":{"id":"89ad4ac3-39f7-470e-963a-56509c546377"}}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::unavailable);
        identity = parse_musicbrainz_response(with_credit(
            R"([{"name":"Not an ID","artist":{"id":"../../bad"}}])"), "disc");
        check(identity.candidates[0].metadata.artist_identity.status == ArtistIdentityStatus::unavailable);
        check(parse_musicbrainz_response(R"({"releases":[]})", "disc").status == MetadataStatus::not_found);
        const std::string multiple = R"({"releases":[{"id":"a","media":[{"discs":[{"id":"disc"}],"tracks":[]}]},{"id":"b","media":[{"discs":[{"id":"disc"}],"tracks":[]}]}]})";
        check(parse_musicbrainz_response(multiple, "disc").status == MetadataStatus::ambiguous);
        bool bad_position = false;
        try { (void)parse_musicbrainz_response(R"({"releases":[{"media":[{"discs":[{"id":"disc"}],"tracks":[{"position":2}]}]}]})", "disc"); }
        catch (const std::runtime_error&) { bad_position = true; }
        check(bad_position);
        bool rejected = false; try { (void)parse_musicbrainz_response("{", "disc"); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected);
        bool deep = false;
        try { (void)parse_musicbrainz_response(std::string(33, '[') + "0" + std::string(33, ']'), "disc"); }
        catch (const std::runtime_error&) { deep = true; }
        check(deep);
        bool long_string = false;
        try { (void)parse_musicbrainz_response("{\"releases\":[],\"extra\":\"" + std::string(4097, 'x') + "\"}", "disc"); }
        catch (const std::runtime_error&) { long_string = true; }
        check(long_string);
        auto art = parse_cover_art_response(R"({"images":[{"front":true,"thumbnails":{"500":"https://archive.org/front.jpg"}}]})");
        check(art.status == ArtworkStatus::available && art.image_url == "https://archive.org/front.jpg");
        art = parse_cover_art_response(R"({"images":[{"front":true,"image":"http://coverartarchive.org/release/a/front.jpg"}]})");
        check(art.image_url == "https://coverartarchive.org/release/a/front.jpg");
        check(parse_cover_art_response(R"({"images":[]})").status == ArtworkStatus::unavailable);
        std::cout << "PASS: metadata JSON states, model and artwork\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
