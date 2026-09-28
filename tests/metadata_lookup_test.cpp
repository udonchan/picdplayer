#include "metadata_lookup.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("metadata lookup test failed"); }

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
        check(musicbrainz_calls == 2 && cover_art_calls == 1);

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
        std::cout << "PASS: metadata lookup retry, CAA routing, and cancellation fixtures\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
