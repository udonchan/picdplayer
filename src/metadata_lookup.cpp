#include "metadata_lookup.hpp"
#include "http_client.hpp"
#include "metadata_parser.hpp"
#include "musicbrainz_disc_id.hpp"
#include "cd_device.hpp"
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
constexpr std::size_t metadata_limit = 2 * 1024 * 1024;
constexpr std::size_t artwork_json_limit = 512 * 1024;
std::mutex rate_mutex;
std::chrono::steady_clock::time_point last_musicbrainz_request{};

bool safe_key(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char c : value) if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
std::optional<std::string> read_cache(const std::filesystem::path& path, std::size_t maximum) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return std::nullopt;
    if (size > maximum) throw std::runtime_error("cached response exceeds size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void write_cache(const std::filesystem::path& path, std::string_view body) {
    std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return;
    auto temporary = path; temporary += ".tmp";
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); if (!output) return; output.write(body.data(), static_cast<std::streamsize>(body.size())); if (!output) return; }
    std::filesystem::rename(temporary, path, ec);
    if (ec) std::filesystem::remove(temporary, ec);
}
std::string fetch_musicbrainz(const std::string& id) {
    HttpClient client;
    const auto url = "https://musicbrainz.org/ws/2/discid/" + HttpClient::escape(id) +
        "?fmt=json&cdstubs=no&inc=recordings%2Bartist-credits%2Brelease-groups";
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::unique_lock lock(rate_mutex);
        const auto allowed = last_musicbrainz_request + std::chrono::milliseconds(1100);
        if (const auto now = std::chrono::steady_clock::now(); now < allowed) std::this_thread::sleep_until(allowed);
        last_musicbrainz_request = std::chrono::steady_clock::now(); lock.unlock();
        const auto response = client.get(url, metadata_limit);
        if (response.status == 404) return R"({"releases":[]})";
        if (response.status == 200) {
            if (!response.content_type.starts_with("application/json")) throw std::runtime_error("MusicBrainz returned non-JSON content");
            return response.body;
        }
        if (response.status != 429 && response.status != 503)
            throw std::runtime_error("MusicBrainz HTTP status " + std::to_string(response.status));
    }
    throw std::runtime_error("MusicBrainz service remained busy after retries");
}
ArtworkInfo fetch_artwork(const std::string& release_id, const MetadataOptions& options, bool& cache_hit) {
    cache_hit = false;
    if (!safe_key(release_id)) throw std::runtime_error("invalid release ID");
    const auto path = options.cache_directory / "cover-art" / (release_id + ".json");
    std::optional<std::string> body;
    if (options.use_cache && !options.cache_directory.empty()) { body = read_cache(path, artwork_json_limit); cache_hit = body.has_value(); }
    if (!body) {
        HttpClient client;
        const auto response = client.get("https://coverartarchive.org/release/" + HttpClient::escape(release_id) + "/", artwork_json_limit);
        if (response.status == 404) { ArtworkInfo result; result.status = ArtworkStatus::unavailable; return result; }
        if (response.status != 200) throw std::runtime_error("Cover Art HTTP status " + std::to_string(response.status));
        if (!response.content_type.starts_with("application/json")) throw std::runtime_error("Cover Art returned non-JSON content");
        body = response.body;
        if (options.use_cache && !options.cache_directory.empty()) write_cache(path, *body);
    }
    return parse_cover_art_response(*body);
}
void print_result(const MetadataResult& result) {
    const auto printable = [](std::string_view value) {
        std::string output; output.reserve(value.size());
        for (unsigned char c : value) {
            if (c >= 0x20 && c != 0x7f && c != '\\' && c != '"') output += static_cast<char>(c);
            else if (c == '\\' || c == '"') { output += '\\'; output += static_cast<char>(c); }
            else output += '?';
            if (output.size() >= 4096) break;
        }
        return output;
    };
    std::cout << "metadata: disc_id=" << result.disc_id << '\n';
    if (!result.toc.empty()) std::cout << "metadata: toc=" << result.toc << '\n';
    std::cout << "metadata: status=" << metadata_status_name(result.status)
              << " candidates=" << result.candidates.size()
              << " cache=" << (result.from_cache ? "hit" : "miss") << '\n';
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        const auto& disc = result.candidates[i].metadata;
        std::cout << "metadata: candidate=" << i + 1 << " release_id=" << disc.release_id
                  << " medium=" << disc.medium_position << " title=\"" << printable(disc.album_title)
                  << "\" artist=\"" << printable(disc.album_artist) << "\" tracks=" << disc.tracks.size() << '\n';
        for (const auto& track : disc.tracks)
            std::cout << "metadata: track=" << track.track_number << " title=\"" << printable(track.title)
                      << "\" artist=\"" << printable(track.artist) << "\"\n";
    }
    if (result.selected) {
        const char* art = result.artwork.status == ArtworkStatus::available ? "AVAILABLE" :
                          result.artwork.status == ArtworkStatus::unavailable ? "UNAVAILABLE" :
                          result.artwork.status == ArtworkStatus::error ? "ERROR" : "NOT_REQUESTED";
        std::cout << "metadata: cover_art=" << art;
        if (!result.artwork.image_url.empty()) std::cout << " url=" << result.artwork.image_url;
        if (!result.artwork.error.empty()) std::cout << " error=\"" << result.artwork.error << '"';
        std::cout << '\n';
    }
}
}

MetadataResult lookup_musicbrainz_id(const std::string& disc_id, const MetadataOptions& options) {
    if (!safe_key(disc_id)) throw std::invalid_argument("invalid MusicBrainz Disc ID");
    const auto path = options.cache_directory / "metadata" / (disc_id + ".json");
    std::optional<std::string> body;
    bool cache_hit = false;
    if (options.use_cache && !options.cache_directory.empty()) { body = read_cache(path, metadata_limit); cache_hit = body.has_value(); }
    if (!body) {
        body = fetch_musicbrainz(disc_id);
        if (options.use_cache && !options.cache_directory.empty()) write_cache(path, *body);
    }
    auto result = parse_musicbrainz_response(*body, disc_id); result.from_cache = cache_hit;
    if (result.selected) {
        bool artwork_cache = false;
        try { result.artwork = fetch_artwork(result.candidates[*result.selected].metadata.release_id, options, artwork_cache); }
        catch (const std::exception& e) { result.artwork.status = ArtworkStatus::error; result.artwork.error = e.what(); }
    }
    return result;
}
MetadataResult lookup_musicbrainz_disc(const DiscToc& toc, const MetadataOptions& options) {
    const auto identity = calculate_musicbrainz_disc_id(toc);
    auto result = lookup_musicbrainz_id(identity.id, options); result.toc = identity.toc;
    for (const auto& candidate : result.candidates) {
        if (candidate.metadata.tracks.size() != toc.tracks.size()) {
            result.status = MetadataStatus::error; result.selected.reset();
            result.error = "MusicBrainz track count does not match physical TOC";
            result.artwork = {};
            break;
        }
    }
    return result;
}
void probe_metadata_device(const std::string& device, const MetadataOptions& options) { print_result(lookup_musicbrainz_disc(read_cd_toc(device), options)); }
void probe_metadata_id(const std::string& disc_id, const MetadataOptions& options) { print_result(lookup_musicbrainz_id(disc_id, options)); }
