#include "artist_background_parser.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
constexpr std::size_t response_limit = 512 * 1024;
constexpr std::size_t image_limit = 32;

bool valid_mbid(std::string_view value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
bool safe_asset_name(std::string_view value) {
    if (value.empty() || value.size() > 255 || value.front() == '.') return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || c == '.';
    });
}

unsigned dimension(const Json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) return 0;
    std::string value;
    if (it->is_string()) value = it->get<std::string>();
    else if (it->is_number_unsigned()) value = std::to_string(it->get<unsigned long long>());
    else return 0;
    if (value.empty() || value.size() > 5 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) return 0;
    const auto parsed = std::stoul(value);
    return parsed <= 16384 ? static_cast<unsigned>(parsed) : 0;
}

std::string string_field(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
}

std::vector<ArtistBackgroundCandidate> parse_artist_backgrounds(
    std::string_view body, std::string_view artist_mbid) {
    if (!valid_mbid(artist_mbid)) throw std::invalid_argument("invalid artist MBID");
    if (body.size() > response_limit) throw std::runtime_error("fanart.tv response exceeds size limit");
    Json root;
    try {
        root = Json::parse(body.begin(), body.end(), [](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 16) throw std::runtime_error("fanart.tv JSON exceeds nesting limit");
            if ((event == Json::parse_event_t::key || event == Json::parse_event_t::value) &&
                value.is_string() && value.get_ref<const std::string&>().size() > 4096)
                throw std::runtime_error("fanart.tv JSON string exceeds size limit");
            return true;
        });
    } catch (const Json::exception&) {
        throw std::runtime_error("invalid fanart.tv JSON");
    }
    if (!root.is_object()) throw std::runtime_error("fanart.tv response is not an object");
    const auto response_mbid = string_field(root, "mbid_id");
    if (response_mbid != artist_mbid) throw std::runtime_error("fanart.tv artist MBID mismatch");
    const auto images = root.find("artistbackground");
    if (images == root.end()) return {};
    if (!images->is_array() || images->size() > image_limit)
        throw std::runtime_error("fanart.tv artistbackground is not a bounded array");
    const std::string prefix = "https://assets.fanart.tv/fanart/music/" +
                               std::string(artist_mbid) + "/artistbackground/";
    std::vector<ArtistBackgroundCandidate> result;
    for (const auto& image : *images) {
        if (!image.is_object()) continue;
        ArtistBackgroundCandidate candidate{
            string_field(image, "id"), string_field(image, "url"),
            dimension(image, "width"), dimension(image, "height")};
        if (candidate.id.empty() || candidate.id.size() > 64 ||
            !std::all_of(candidate.id.begin(), candidate.id.end(), [](unsigned char c) { return std::isdigit(c); }) ||
            !candidate.url.starts_with(prefix) || !safe_asset_name(
                std::string_view(candidate.url).substr(prefix.size())) ||
            candidate.width == 0 || candidate.height == 0) continue;
        result.push_back(std::move(candidate));
    }
    return result;
}
