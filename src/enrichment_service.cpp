#include "enrichment_service.hpp"

#include "artist_background_worker.hpp"
#include "artist_background_image_worker.hpp"
#include "logger.hpp"
#include "metadata_lookup.hpp"
#include "metadata_session.hpp"
#include "metadata_worker.hpp"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <utility>
#include <random>
#include <algorithm>
#include <set>

class EnrichmentService::Implementation {
public:
    explicit Implementation(bool enabled, std::string directory,
                            const ArtistBackgroundPolicy& policy, std::string artist_key)
        : cache_directory(std::move(directory)), background_policy(policy), background_key(std::move(artist_key)) {
        if (!enabled) return;
        // Keep this path for serving validated same-origin artwork after the
        // worker has finished. The worker receives its own copy.
        MetadataOptions options{.cache_directory = cache_directory, .use_cache = true, .cancelled = {}, .http_get = {}};
        options.include_artwork = false;
        worker = std::make_unique<MetadataWorker>(
            [options = std::move(options)](const DiscToc& toc, const MetadataWorker::Cancelled& cancelled) mutable {
                options.cancelled = cancelled;
                return lookup_musicbrainz_disc(toc, options);
            });
        MetadataOptions artwork_options{.cache_directory = cache_directory, .use_cache = true,
                                        .cancelled = {}, .http_get = {}};
        artwork_worker = std::make_unique<ArtworkWorker>(
            [options = std::move(artwork_options)](const std::string& release_id,
                                                  const MetadataWorker::Cancelled& cancelled) mutable {
                options.cancelled = cancelled;
                return lookup_cover_art_release(release_id, options);
            });
    }

    void clear_background() {
        if (background_worker) background_worker->cancel_pending();
        if (image_worker) image_worker->cancel_pending();
        background_request.reset();
        image_request.reset();
        background_lookup = {};
        background_images.clear();
        retained_bytes = 0;
        candidate_ids.clear();
        candidate_urls.clear();
        image_index = 0;
    }

    void start_image() {
        image_request.reset();
        if (!background_request || background_images.size() >= 3 ||
            retained_bytes >= aggregate_limit) return;
        while (image_index < background_lookup.images.size() && image_index < 32) {
            const auto& candidate = background_lookup.images[image_index++];
            const bool duplicate = candidate_ids.contains(candidate.id) || candidate_urls.contains(candidate.url);
            candidate_ids.insert(candidate.id);
            candidate_urls.insert(candidate.url);
            if (duplicate) continue;
            image_request = ArtistBackgroundImageRequest{background_request->generation,
                background_request->artist_mbid, candidate};
            image_worker->request(*image_request);
            return;
        }
    }

    void ensure_background_workers() {
        if (!background_worker) {
            background_worker = std::make_unique<ArtistBackgroundWorker>(
                [key = background_key](
                    const std::string& mbid, const ArtistBackgroundWorker::Cancelled& cancelled) {
                    return lookup_artist_backgrounds(mbid, key, cancelled, {}, {});
                });
            image_worker = std::make_unique<ArtistBackgroundImageWorker>(
                [](const ArtistBackgroundImageRequest& input,
                   const ArtistBackgroundImageWorker::Cancelled& cancelled) {
                    return load_artist_background_image(input.artist_mbid, input.candidate, {}, cancelled);
                });
        }
    }

    void sync_background_request() {
        if (!background_policy.effective() || background_key.empty()) {
            clear_background();
            return;
        }
        const auto artist = selected_artist_identity(session.snapshot());
        if (artist.status != ArtistIdentityStatus::available || artist.mbid.empty()) {
            clear_background();
            return;
        }
        ArtistBackgroundRequest wanted{session.generation(), artist.mbid};
        if (background_request && background_request->generation == wanted.generation &&
            background_request->artist_mbid == wanted.artist_mbid) return;
        clear_background();
        ensure_background_workers();
        background_worker->request(wanted);
        background_request = std::move(wanted);
        background_lookup = {};
    }

    MetadataSession session;
    std::unique_ptr<MetadataWorker> worker;
    std::unique_ptr<ArtworkWorker> artwork_worker;
    std::unique_ptr<ArtistBackgroundWorker> background_worker;
    std::unique_ptr<ArtistBackgroundImageWorker> image_worker;
    std::string cache_directory;
    const ArtistBackgroundPolicy& background_policy;
    std::string background_key;
    std::optional<ArtistBackgroundRequest> background_request;
    ArtistBackgroundLookup background_lookup;
    std::optional<ArtistBackgroundImageRequest> image_request;
    std::size_t image_index = 0;
    struct RetainedImage {
        ArtistBackgroundImage image;
        std::string reference;
    };
    std::vector<RetainedImage> background_images;
    static constexpr std::size_t aggregate_limit = 16 * 1024 * 1024;
    std::size_t retained_bytes = 0;
    std::set<std::string> candidate_ids, candidate_urls;
    const std::string reference_nonce = std::to_string(std::random_device{}()) + "-" +
                                        std::to_string(std::random_device{}());
    std::uint64_t asset_sequence = 0;
};

EnrichmentService::EnrichmentService(bool enabled, std::string cache_directory,
                                     const ArtistBackgroundPolicy& background_policy,
                                     std::string artist_background_key)
    : implementation_(std::make_unique<Implementation>(enabled, std::move(cache_directory),
                                                        background_policy, std::move(artist_background_key))) {}
EnrichmentService::~EnrichmentService() = default;

void EnrichmentService::begin_if_needed(const DiscToc& toc) {
    if (!implementation_->worker) return;
    if (const auto request = implementation_->session.begin_if_needed(toc)) {
        stop_background();
        implementation_->artwork_worker->cancel_pending();
        implementation_->worker->request(*request);
    }
}

void EnrichmentService::invalidate() {
    if (implementation_->worker) implementation_->worker->cancel_pending();
    if (implementation_->artwork_worker) implementation_->artwork_worker->cancel_pending();
    stop_background();
    implementation_->session.invalidate();
}

void EnrichmentService::stop_background() {
    implementation_->clear_background();
}

void EnrichmentService::poll() {
    if (!implementation_->worker) return;
    MetadataWorkerResult result;
    while (implementation_->worker->pop(result)) {
        if (!implementation_->session.apply(std::move(result))) continue;
        const auto& current = implementation_->session.snapshot();
        if (current.status == MetadataStatus::available && current.selected &&
            *current.selected < current.candidates.size() &&
            current.artwork.status == ArtworkStatus::not_requested)
            implementation_->artwork_worker->request({implementation_->session.generation(),
                current.candidates[*current.selected].metadata.release_id});
    }
    ArtworkWorkerResult artwork_result;
    while (implementation_->artwork_worker->pop(artwork_result))
        implementation_->session.apply_artwork(artwork_result.request.generation,
            artwork_result.request.release_id, std::move(artwork_result.artwork));
    implementation_->sync_background_request();
    if (implementation_->background_worker) {
        ArtistBackgroundWorkerResult background_result;
        while (implementation_->background_worker->pop(background_result)) {
            const auto& requested = implementation_->background_request;
            if (!requested || background_result.request.generation != requested->generation ||
                background_result.request.artist_mbid != requested->artist_mbid) continue;
            const auto artist = selected_artist_identity(implementation_->session.snapshot());
            if (artist.status != ArtistIdentityStatus::available ||
                artist.mbid != requested->artist_mbid ||
                implementation_->session.generation() != requested->generation) continue;
            implementation_->background_lookup = std::move(background_result.lookup);
            if (implementation_->background_lookup.status == ArtistBackgroundLookupStatus::available)
                implementation_->start_image();
            const char* status = implementation_->background_lookup.status == ArtistBackgroundLookupStatus::available
                ? "AVAILABLE" : implementation_->background_lookup.status == ArtistBackgroundLookupStatus::unavailable
                ? "UNAVAILABLE" : "ERROR";
            log_info("artist_background") << "candidate_status=" << status
                                          << " count=" << implementation_->background_lookup.images.size();
        }
    }
    if (implementation_->image_worker) {
        ArtistBackgroundImageWorkerResult image_result;
        while (implementation_->image_worker->pop(image_result)) {
            const auto& wanted = implementation_->image_request;
            if (!implementation_->background_policy.effective() || !wanted ||
                image_result.request.generation != wanted->generation ||
                image_result.request.artist_mbid != wanted->artist_mbid ||
                image_result.request.candidate.id != wanted->candidate.id ||
                image_result.request.candidate.url != wanted->candidate.url ||
                implementation_->session.generation() != wanted->generation ||
                selected_artist_identity(implementation_->session.snapshot()).mbid != wanted->artist_mbid)
                continue;
            if (image_result.image && image_result.image->bytes.size() <=
                Implementation::aggregate_limit - implementation_->retained_bytes &&
                std::none_of(implementation_->background_images.begin(), implementation_->background_images.end(),
                    [&](const auto& retained) { return retained.image.bytes == image_result.image->bytes; })) {
                implementation_->retained_bytes += image_result.image->bytes.size();
                auto reference = "/api/presentation/artwork/background/" + implementation_->reference_nonce +
                    "-" + std::to_string(++implementation_->asset_sequence);
                implementation_->background_images.push_back({std::move(*image_result.image), std::move(reference)});
            }
            implementation_->start_image();
        }
    }
}

std::optional<std::string> EnrichmentService::background_url() const {
    const auto urls = background_urls();
    return urls.empty() ? std::nullopt : std::optional<std::string>(urls.front());
}

std::vector<std::string> EnrichmentService::background_urls() const {
    const auto& state = *implementation_;
    if (!state.background_policy.effective() || !state.background_request ||
        state.background_request->generation != state.session.generation() ||
        selected_artist_identity(state.session.snapshot()).mbid != state.background_request->artist_mbid) return {};
    std::vector<std::string> urls;
    for (const auto& retained : state.background_images) urls.push_back(retained.reference);
    return urls;
}

std::optional<EnrichmentArtworkAsset> EnrichmentService::background_asset(std::string_view reference) const {
    const auto urls = background_urls();
    if (std::find(urls.begin(), urls.end(), reference) == urls.end()) return std::nullopt;
    for (const auto& retained : implementation_->background_images)
        if (retained.reference == reference)
            return EnrichmentArtworkAsset{retained.image.mime_type, retained.image.bytes};
    return std::nullopt;
}

const MetadataResult& EnrichmentService::snapshot() const { return implementation_->session.snapshot(); }
std::uint64_t EnrichmentService::generation() const { return implementation_->session.generation(); }
bool EnrichmentService::select_candidate(std::uint64_t generation, std::size_t index) {
    if (!implementation_->worker) return false;
    const auto& before = implementation_->session.snapshot();
    if (generation == implementation_->session.generation() && before.selected == index &&
        before.status == MetadataStatus::available) return true;
    if (!implementation_->session.select_candidate(generation, index)) return false;
    const auto& selected = implementation_->session.snapshot().candidates[index].metadata;
    implementation_->artwork_worker->request({generation, selected.release_id});
    implementation_->sync_background_request();
    return true;
}
bool EnrichmentService::decline_candidates(std::uint64_t generation) {
    if (!implementation_->worker || !implementation_->session.decline_candidates(generation)) return false;
    implementation_->artwork_worker->cancel_pending();
    stop_background();
    return true;
}

namespace {
bool safe_key(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char c : value)
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
std::optional<std::filesystem::path> cover_path(const EnrichmentService::Implementation& implementation) {
    const auto& result = implementation.session.snapshot();
    if (!result.selected || *result.selected >= result.candidates.size()) return std::nullopt;
    const auto& release = result.candidates[*result.selected].metadata.release_id;
    if (implementation.cache_directory.empty() || !safe_key(release)) return std::nullopt;
    return std::filesystem::path(implementation.cache_directory) / "cover-art" / (release + ".image");
}
std::optional<std::string> mime_for(std::string_view bytes) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 && static_cast<unsigned char>(bytes[2]) == 0xff) return "image/jpeg";
    if (bytes.size() >= 8 && bytes.substr(0, 8) == "\x89PNG\r\n\x1a\n") return "image/png";
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") return "image/webp";
    return std::nullopt;
}
}

bool EnrichmentService::has_cover_asset() const {
    if (implementation_->session.snapshot().artwork.status != ArtworkStatus::available) return false;
    const auto path = cover_path(*implementation_);
    std::error_code error;
    return path && std::filesystem::is_regular_file(*path, error) && !error;
}

std::optional<EnrichmentArtworkAsset> EnrichmentService::cover_asset() const {
    if (implementation_->session.snapshot().artwork.status != ArtworkStatus::available) return std::nullopt;
    const auto path = cover_path(*implementation_);
    if (!path) return std::nullopt;
    std::error_code error;
    const auto size = std::filesystem::file_size(*path, error);
    if (error || size == 0 || size > 4 * 1024 * 1024) return std::nullopt;
    std::ifstream input(*path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    const auto mime = mime_for(bytes);
    if (!mime) return std::nullopt;
    return EnrichmentArtworkAsset{*mime, std::move(bytes)};
}
