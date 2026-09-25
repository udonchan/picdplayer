#include "enrichment_service.hpp"

#include "metadata_lookup.hpp"
#include "metadata_session.hpp"
#include "metadata_worker.hpp"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <utility>

class EnrichmentService::Implementation {
public:
    explicit Implementation(bool enabled, std::string directory) : cache_directory(std::move(directory)) {
        if (!enabled) return;
        // Keep this path for serving validated same-origin artwork after the
        // worker has finished. The worker receives its own copy.
        MetadataOptions options{.cache_directory = cache_directory, .use_cache = true, .cancelled = {}};
        worker = std::make_unique<MetadataWorker>(
            [options = std::move(options)](const DiscToc& toc, const MetadataWorker::Cancelled& cancelled) mutable {
                options.cancelled = cancelled;
                return lookup_musicbrainz_disc(toc, options);
            });
    }

    MetadataSession session;
    std::unique_ptr<MetadataWorker> worker;
    std::string cache_directory;
};

EnrichmentService::EnrichmentService(bool enabled, std::string cache_directory)
    : implementation_(std::make_unique<Implementation>(enabled, std::move(cache_directory))) {}
EnrichmentService::~EnrichmentService() = default;

void EnrichmentService::begin_if_needed(const DiscToc& toc) {
    if (!implementation_->worker) return;
    if (const auto request = implementation_->session.begin_if_needed(toc))
        implementation_->worker->request(*request);
}

void EnrichmentService::invalidate() {
    if (implementation_->worker) implementation_->worker->cancel_pending();
    implementation_->session.invalidate();
}

void EnrichmentService::poll() {
    if (!implementation_->worker) return;
    MetadataWorkerResult result;
    while (implementation_->worker->pop(result))
        implementation_->session.apply(std::move(result));
}

const MetadataResult& EnrichmentService::snapshot() const { return implementation_->session.snapshot(); }

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
    const auto path = cover_path(*implementation_);
    std::error_code error;
    return path && std::filesystem::is_regular_file(*path, error) && !error;
}

std::optional<EnrichmentArtworkAsset> EnrichmentService::cover_asset() const {
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
