#include "metadata_lookup.hpp"
#include "metadata_worker.hpp"
#include "musicbrainz_disc_id.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
void check(bool value, std::string_view detail = {}) {
    if (!value) throw std::runtime_error("metadata lookup test failed: " + std::string(detail));
}

constexpr const char* musicbrainz_body = R"({"releases":[{"id":"release","title":"Album","media":[{"discs":[{"id":"disc"}],"tracks":[{"position":1,"title":"Song"}]}]}]})";
constexpr const char* cover_art_body = R"({"images":[{"front":true,"thumbnails":{"500":"https://coverartarchive.org/release/release/cover.jpg"}}]})";
}

int main() {
    try {
        int musicbrainz_calls = 0;
        int cover_art_calls = 0;
        MetadataOptions options{
            .cache_directory = {},
            .use_cache = false,
            .cancelled = {},
            .http_get = [&](std::string_view url, std::size_t, const std::function<bool()>&, RedirectPolicy redirects) {
                if (url.starts_with("https://musicbrainz.org/")) {
                    check(redirects == RedirectPolicy::reject);
                    ++musicbrainz_calls;
                    if (musicbrainz_calls == 1)
                        return HttpResponse{.status = 429, .content_type = {}, .body = {}, .retry_after_seconds = 0};
                    if (musicbrainz_calls == 2)
                        return HttpResponse{.status = 503, .content_type = {}, .body = {}, .retry_after_seconds = {}};
                    return HttpResponse{.status = 200, .content_type = "application/json", .body = musicbrainz_body,
                                        .retry_after_seconds = {}};
                }
                check(url.starts_with("https://coverartarchive.org/release/release/"));
                check(redirects == RedirectPolicy::follow_cover_art_archive);
                ++cover_art_calls;
                return HttpResponse{.status = 200, .content_type = "application/json", .body = cover_art_body,
                                    .retry_after_seconds = {}};
            }};
        const auto result = lookup_musicbrainz_id("disc", options);
        check(result.status == MetadataStatus::available && result.selected == 0);
        check(result.artwork.status == ArtworkStatus::available);
        check(musicbrainz_calls == 3 && cover_art_calls == 1);

        MetadataOptions selected_artwork_options{
            .cache_directory = {}, .use_cache = false, .cancelled = {},
            .http_get = [](std::string_view url, std::size_t, const std::function<bool()>&,
                           RedirectPolicy redirects) {
                check(url == "https://coverartarchive.org/release/chosen/");
                check(redirects == RedirectPolicy::follow_cover_art_archive);
                return HttpResponse{.status = 404, .content_type = {}, .body = {}, .retry_after_seconds = {}};
            }};
        check(lookup_cover_art_release("chosen", selected_artwork_options).status == ArtworkStatus::unavailable);
        bool invalid_release_rejected = false;
        try { (void)lookup_cover_art_release("../invalid", selected_artwork_options); }
        catch (const std::runtime_error&) { invalid_release_rejected = true; }
        check(invalid_release_rejected);

        bool cancelled = false;
        options.cancelled = [&] { return cancelled; };
        options.http_get = [&](std::string_view, std::size_t, const std::function<bool()>&, RedirectPolicy) {
            cancelled = true;
            return HttpResponse{.status = 503, .content_type = {}, .body = {}, .retry_after_seconds = 0};
        };
        bool cancellation_observed = false;
        try { (void)lookup_musicbrainz_id("disc", options); }
        catch (const std::runtime_error& error) { cancellation_observed = std::string_view(error.what()) == "metadata lookup cancelled"; }
        check(cancellation_observed);

        MetadataOptions failing_options{
            .cache_directory = {},
            .use_cache = false,
            .cancelled = {},
            .http_get = [](std::string_view, std::size_t, const std::function<bool()>&, RedirectPolicy) -> HttpResponse {
                throw std::runtime_error("simulated connection failure");
            }};
        MetadataWorker worker([failing_options = std::move(failing_options)](
                                  const DiscToc&, const MetadataWorker::Cancelled& cancelled) mutable {
            failing_options.cancelled = cancelled;
            return lookup_musicbrainz_id("disc", failing_options);
        });
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
        worker.request({42, toc});
        MetadataWorkerResult failure{};
        for (int i = 0; i < 500 && !worker.pop(failure); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(failure.generation == 42, "worker did not return generation 42");
        check(failure.metadata.status == MetadataStatus::error, "worker did not map lookup failure to ERROR");
        check(failure.metadata.error == "simulated connection failure", "worker did not preserve lookup failure message");

        const auto non_one_based_toc = make_audio_toc(3, std::vector<std::int32_t>{0, 75}, 150);
        const auto non_one_based_id = calculate_musicbrainz_disc_id(non_one_based_toc).id;
        bool return_short_medium = false;
        MetadataOptions mapping_options{
            .cache_directory = {},
            .use_cache = false,
            .cancelled = {},
            .http_get = [&](std::string_view url, std::size_t, const std::function<bool()>&, RedirectPolicy) {
                if (url.starts_with("https://musicbrainz.org/")) {
                    const auto body = std::string(R"({"releases":[{"id":"mapping","media":[{"discs":[{"id":")") +
                                      non_one_based_id +
                                      (return_short_medium ? R"("}],"tracks":[{"position":1,"title":"First"}]}]}]})"
                                                           : R"("}],"tracks":[{"position":1,"title":"First"},{"position":2,"title":"Second"}]}]}]})");
                    return HttpResponse{.status = 200, .content_type = "application/json", .body = body,
                                        .retry_after_seconds = {}};
                }
                return HttpResponse{.status = 404, .content_type = {}, .body = {}, .retry_after_seconds = {}};
            }};
        const auto mapped = lookup_musicbrainz_disc(non_one_based_toc, mapping_options);
        check(mapped.status == MetadataStatus::available && mapped.selected == 0,
              "non-one-based TOC lookup was not available");
        check(mapped.candidates[0].metadata.tracks[0].track_number == 3 &&
              mapped.candidates[0].metadata.tracks[1].track_number == 4,
              "medium positions were not mapped to physical track numbers");
        return_short_medium = true;
        const auto mismatched = lookup_musicbrainz_disc(non_one_based_toc, mapping_options);
        check(mismatched.status == MetadataStatus::error && !mismatched.selected,
              "track-count mismatch was not rejected");
        std::cout << "PASS: metadata lookup retry, CAA routing, cancellation, and failure fixtures\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
