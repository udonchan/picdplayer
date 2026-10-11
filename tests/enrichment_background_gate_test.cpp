#include "enrichment_service.hpp"
#include "artist_background_provider.hpp"
#include "metadata_lookup.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
std::atomic<unsigned> background_calls{0};
std::atomic<unsigned> cover_calls{0};
std::atomic<unsigned> metadata_calls{0};
#define check(value) do { if (!(value)) throw std::runtime_error("enrichment background gate test failed at line " + std::to_string(__LINE__)); } while (0)
void await_metadata(EnrichmentService& service) {
    for (unsigned i = 0; i < 1000; ++i) {
        service.poll();
        if (service.snapshot().status == MetadataStatus::available) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("fake metadata result did not arrive");
}
}

// Link-time fakes exercise the production service without adding runtime hooks.
MetadataResult lookup_musicbrainz_disc(const DiscToc&, const MetadataOptions&) {
    ++metadata_calls;
    MetadataResult result;
    result.status = MetadataStatus::available;
    result.selected = 0;
    result.candidates.resize(2);
    for (unsigned i = 0; i < 2; ++i) {
        result.candidates[i].metadata.release_id = "fixture-release-" + std::to_string(i);
        result.candidates[i].metadata.artist_identity = {ArtistIdentityStatus::available,
            i == 0 ? "0383dadf-2a4e-4d10-a46a-e9e041da8eb3" : "89ad4ac3-39f7-470e-963a-56509c546378"};
    }
    return result;
}
ArtworkInfo lookup_cover_art_release(const std::string&, const MetadataOptions&) {
    ++cover_calls;
    ArtworkInfo result;
    result.status = ArtworkStatus::unavailable;
    return result;
}
ArtistBackgroundLookup lookup_artist_backgrounds(std::string_view, std::string_view,
    const std::function<bool()>&, const FanartHttpGet&, const std::filesystem::path&) {
    ++background_calls;
    return {};
}

int main() {
    try {
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
        for (const auto* key : {"", "synthetic-project-key"}) {
            ArtistBackgroundPolicy policy;
            {
                EnrichmentService service(true, {}, policy, key);
                service.begin_if_needed(toc);
                await_metadata(service);
                for (unsigned i = 0; i < 1000 && service.snapshot().artwork.status == ArtworkStatus::not_requested; ++i) {
                    service.poll();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                check(service.snapshot().artwork.status == ArtworkStatus::unavailable);
                const auto generation = service.generation();
                check(service.snapshot().selected == 0);
                check(selected_artist_identity(service.snapshot()).status == ArtistIdentityStatus::available);
                check(!policy.set_enabled(true));
                for (unsigned i = 0; i < 3; ++i) {
                    check(policy.set_enabled(false));
                    service.stop_background();
                    check(service.select_candidate(generation, 1));
                    service.poll();
                    check(service.decline_candidates(generation));
                    service.poll();
                    check(service.select_candidate(generation, 0));
                    service.poll();
                    check(!policy.effective());
                }
                service.invalidate();
                check(!service.select_candidate(generation, 0));
                service.begin_if_needed(toc);
                await_metadata(service);
                check(service.generation() != generation);
                check(background_calls == 0 && !service.has_cover_asset());
            }
            // Join all worker threads before checking the final request count.
            check(background_calls == 0);
            {
                EnrichmentService disabled(false, {}, policy, key);
                disabled.begin_if_needed(toc);
                disabled.poll();
                check(disabled.snapshot().status == MetadataStatus::not_requested);
            }
        }
        check(metadata_calls == 4 && cover_calls > 0 && background_calls == 0);
        std::cout << "PASS: key absent/present, OFF, selection, decline and disc generations make zero background requests\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
