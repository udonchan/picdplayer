#include "metadata_parser.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

using Json = nlohmann::json;
namespace {
std::string text(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
std::string artist_credit(const Json& object) {
    const auto it = object.find("artist-credit");
    if (it == object.end() || !it->is_array()) return {};
    std::string value;
    for (const auto& part : *it) {
        if (!part.is_object()) throw std::runtime_error("artist-credit item is not an object");
        auto name = text(part, "name");
        if (name.empty() && part.contains("artist") && part["artist"].is_object())
            name = text(part["artist"], "name");
        value += name;
        value += text(part, "joinphrase");
    }
    return value;
}
}

const char* metadata_status_name(MetadataStatus status) {
    switch (status) {
    case MetadataStatus::not_requested: return "NOT_REQUESTED";
    case MetadataStatus::loading: return "LOADING";
    case MetadataStatus::available: return "AVAILABLE";
    case MetadataStatus::not_found: return "NOT_FOUND";
    case MetadataStatus::ambiguous: return "AMBIGUOUS";
    case MetadataStatus::error: return "ERROR";
    }
    return "ERROR";
}

MetadataResult parse_musicbrainz_response(std::string_view input, std::string_view disc_id) {
    MetadataResult result; result.disc_id = disc_id;
    Json root;
    try { root = Json::parse(input.begin(), input.end()); }
    catch (const Json::exception& e) { throw std::runtime_error(std::string("invalid MusicBrainz JSON: ") + e.what()); }
    if (!root.is_object()) throw std::runtime_error("MusicBrainz response is not an object");
    const auto releases = root.find("releases");
    if (releases == root.end() || !releases->is_array()) throw std::runtime_error("MusicBrainz response has no releases array");
    if (releases->size() > 100) throw std::runtime_error("MusicBrainz response has too many releases");
    for (const auto& release : *releases) {
        if (!release.is_object()) throw std::runtime_error("release is not an object");
        const auto media = release.find("media");
        if (media == release.end() || !media->is_array()) throw std::runtime_error("release has no media array");
        for (const auto& medium : *media) {
            if (!medium.is_object()) throw std::runtime_error("medium is not an object");
            bool matches = false;
            if (const auto discs = medium.find("discs"); discs != medium.end()) {
                if (!discs->is_array()) throw std::runtime_error("medium discs is not an array");
                for (const auto& disc : *discs)
                    if (disc.is_object() && text(disc, "id") == disc_id) matches = true;
            }
            if (!matches) continue;
            DiscMetadata value;
            value.release_id = text(release, "id");
            value.album_title = text(release, "title");
            value.album_artist = artist_credit(release);
            value.country = text(release, "country"); value.date = text(release, "date");
            if (release.contains("release-group") && release["release-group"].is_object())
                value.release_group_id = text(release["release-group"], "id");
            if (medium.contains("position") && medium["position"].is_number_integer()) value.medium_position = medium["position"].get<int>();
            value.medium_title = text(medium, "title");
            const auto tracks = medium.find("tracks");
            if (tracks == medium.end() || !tracks->is_array()) throw std::runtime_error("matching medium has no tracks array");
            for (const auto& track : *tracks) {
                if (!track.is_object() || !track.contains("position") || !track["position"].is_number_integer())
                    throw std::runtime_error("track position is missing");
                TrackMetadata item;
                item.track_number = track["position"].get<int>();
                item.title = text(track, "title"); item.artist = artist_credit(track);
                if (track.contains("length") && track["length"].is_number_integer()) item.source_length_ms = track["length"].get<std::int64_t>();
                if (track.contains("recording") && track["recording"].is_object()) {
                    item.recording_id = text(track["recording"], "id");
                    if (item.title.empty()) item.title = text(track["recording"], "title");
                    if (item.artist.empty()) item.artist = artist_credit(track["recording"]);
                }
                value.tracks.push_back(std::move(item));
            }
            for (std::size_t i = 0; i < value.tracks.size(); ++i)
                if (value.tracks[i].track_number != static_cast<int>(i + 1))
                    throw std::runtime_error("track positions are not consecutive");
            result.candidates.push_back({std::move(value)});
        }
    }
    if (result.candidates.empty()) result.status = MetadataStatus::not_found;
    else if (result.candidates.size() == 1) { result.status = MetadataStatus::available; result.selected = 0; }
    else result.status = MetadataStatus::ambiguous;
    return result;
}

ArtworkInfo parse_cover_art_response(std::string_view input) {
    ArtworkInfo result;
    Json root;
    try { root = Json::parse(input.begin(), input.end()); }
    catch (const Json::exception& e) { throw std::runtime_error(std::string("invalid Cover Art JSON: ") + e.what()); }
    if (!root.is_object() || !root.contains("images") || !root["images"].is_array())
        throw std::runtime_error("Cover Art response has no images array");
    for (const auto& image : root["images"]) {
        if (!image.is_object() || !image.value("front", false)) continue;
        if (image.contains("thumbnails") && image["thumbnails"].is_object()) {
            result.image_url = text(image["thumbnails"], "500");
            if (result.image_url.empty()) result.image_url = text(image["thumbnails"], "large");
        }
        if (result.image_url.empty()) result.image_url = text(image, "image");
        constexpr std::string_view insecure_caa = "http://coverartarchive.org/";
        if (result.image_url.starts_with(insecure_caa))
            result.image_url = "https://coverartarchive.org/" + result.image_url.substr(insecure_caa.size());
        if (!result.image_url.empty() && !result.image_url.starts_with("https://"))
            throw std::runtime_error("Cover Art image URL is not HTTPS");
        if (!result.image_url.empty()) { result.status = ArtworkStatus::available; return result; }
    }
    result.status = ArtworkStatus::unavailable;
    return result;
}
