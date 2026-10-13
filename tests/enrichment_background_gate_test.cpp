#include "enrichment_service.hpp"
#include "artist_background_provider.hpp"
#include "artist_background_image.hpp"
#include "metadata_lookup.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef ENABLE_API
#include <array>
#include <cerrno>
#include "api_server.hpp"
#include "presentation_json.hpp"
#include <nlohmann/json.hpp>
#include <csignal>
#include <fstream>
#include <iterator>
#include <poll.h>
#include <unistd.h>
#endif

namespace {
std::atomic<unsigned> background_calls{0};
std::atomic<unsigned> cover_calls{0};
std::atomic<unsigned> metadata_calls{0};
std::atomic<unsigned> image_calls{0};
std::atomic<bool> hold_images{false}, image_started{false}, offline{false};
std::string fixture_png;
#define check(value) do { if (!(value)) throw std::runtime_error("enrichment background gate test failed at line " + std::to_string(__LINE__)); } while (0)
void await_metadata(EnrichmentService& service) {
    for (unsigned i = 0; i < 1000; ++i) {
        service.poll();
        if (service.snapshot().status == MetadataStatus::available) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("fake metadata result did not arrive");
}
template<class Predicate> void await(EnrichmentService& service, Predicate predicate) {
    for (unsigned i = 0; i < 2000; ++i) {
        service.poll();
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("background runtime condition did not arrive");
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
        result.candidates[i].metadata.album_title = "Synthetic release " + std::to_string(i);
        result.candidates[i].metadata.album_artist = "Synthetic artist " + std::to_string(i);
        result.candidates[i].metadata.tracks.push_back({1, "Synthetic track", "Generated pixels", {}, {}});
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
ArtistBackgroundLookup lookup_artist_backgrounds(std::string_view mbid, std::string_view key,
    const std::function<bool()>&, const FanartHttpGet&, const std::filesystem::path& path) {
    ++background_calls;
    check(path.empty() && key == "synthetic-project-key");
    if (offline) return {ArtistBackgroundLookupStatus::error, {}};
    ArtistBackgroundLookup result{ArtistBackgroundLookupStatus::available, {}};
    for (const auto* id : {"1", "2"}) result.images.push_back({id,
        "https://assets.fanart.tv/fanart/music/" + std::string(mbid) + "/artistbackground/fixture.jpg", 1920, 1080});
    return result;
}

std::optional<ArtistBackgroundImage> load_artist_background_image(std::string_view,
    const ArtistBackgroundCandidate& candidate, const std::filesystem::path& path,
    const std::function<bool()>& cancelled, const ArtistImageHttpGet&) {
    ++image_calls;
    check(path.empty());
    if (candidate.id == "1") return std::nullopt;
    image_started = true;
    while (hold_images && !cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return fixture_png.empty() ? ArtistBackgroundImage{"image/jpeg", "synthetic-image-bytes"}
                               : ArtistBackgroundImage{"image/png", fixture_png};
}

#ifdef ENABLE_API
namespace {
volatile std::sig_atomic_t fixture_stopping = 0;
void stop_fixture(int) { fixture_stopping = 1; }
void emit_fixture(const nlohmann::json& value) {
    const auto line = value.dump() + "\n";
    check(line.size() < 4096);
    ssize_t written;
    do { written = ::write(STDOUT_FILENO, line.data(), line.size()); } while (written < 0 && errno == EINTR);
    check(written == static_cast<ssize_t>(line.size()));
}

int serve_fixture(int port, const std::string& image_path) {
    check(port >= 0 && port <= 65535);
    std::ifstream input(image_path, std::ios::binary | std::ios::ate);
    check(input && input.tellg() > 0 && input.tellg() <= 1024 * 1024);
    input.seekg(0);
    fixture_png.assign(std::istreambuf_iterator<char>(input), {});
    check(fixture_png.starts_with(std::string("\x89PNG\r\n\x1a\n", 8)));
    const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 7500);
    ArtistBackgroundPolicy policy(true);
    EnrichmentService enrichment(true, {}, policy, "synthetic-project-key");
    PlayerController player;
    std::optional<DiscToc> disc;
    std::uint64_t disc_generation = 0, revision = 0;
    const auto insert = [&] {
        enrichment.invalidate();
        disc = toc;
        ++disc_generation;
        player.load_disc(toc);
        enrichment.begin_if_needed(toc);
    };
    const auto remove = [&] {
        enrichment.invalidate();
        disc.reset();
        player.remove_disc();
    };
    insert();
    std::string state;
    ApiServer* server_pointer = nullptr;
    const auto refresh = [&] {
        ReadDiagnostics read;
        read.session_id = "fixture-runtime";
        auto next = serialize_presentation_model(make_presentation_model(revision + 1, player.state(),
            disc ? MediaLifecycleState::audio_ready : MediaLifecycleState::no_disc, disc,
            enrichment.snapshot(), {}, read, {}, false, disc_generation, enrichment.generation(),
            enrichment.background_url()));
        if (presentation_json_equal_ignoring_revision(state, next)) return;
        ++revision;
        state = std::move(next);
        if (server_pointer) server_pointer->publish_state(state);
    };
    refresh();
    const ApiCommandHandler commands = [&](const ApiCommand& command) {
        bool accepted = false;
        if (command.type == ApiCommandType::set_artist_background) {
            accepted = policy.set_enabled(command.enabled);
            if (accepted && !command.enabled) enrichment.stop_background();
        } else if (command.type == ApiCommandType::eject) {
            remove();
            accepted = true;
        } else if (command.session_id == "fixture-runtime" && command.disc_generation == disc_generation) {
            if (command.type == ApiCommandType::select_metadata_candidate)
                accepted = enrichment.select_candidate(command.metadata_generation, command.candidate_index);
            else if (command.type == ApiCommandType::decline_metadata_candidates)
                accepted = enrichment.decline_candidates(command.metadata_generation);
        }
        if (accepted) refresh();
        return accepted;
    };
    const ApiBackgroundArtworkProvider images = [&](std::string_view reference) -> std::optional<ApiResponse> {
        const auto image = enrichment.background_asset(reference);
        if (!image) return std::nullopt;
        return ApiResponse{200, image->mime_type, image->bytes};
    };
    // This fixed read-policy view is fixture data, not PlayerSession/store coverage.
    ApiServer server("127.0.0.1", port, [&] { return state; }, commands,
        [&] { return nlohmann::json::parse(state)["read"]["policy"].dump(); }, {}, {}, {},
        [&] { return policy.json(); }, images);
    server_pointer = &server;
    std::signal(SIGTERM, stop_fixture);
    std::signal(SIGINT, stop_fixture);
    emit_fixture({{"fixture_port", server.port()}, {"fixture_pid", getpid()}});
    auto previous_counts = std::array<unsigned, 4>{};
    std::string controls;
    bool quit = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!quit && !fixture_stopping && std::chrono::steady_clock::now() < deadline) {
        pollfd stdin_ready{STDIN_FILENO, POLLIN, 0};
        if (::poll(&stdin_ready, 1, 0) > 0 && (stdin_ready.revents & POLLIN)) {
            char bytes[256];
            const auto size = ::read(STDIN_FILENO, bytes, sizeof(bytes));
            if (size > 0) controls.append(bytes, static_cast<std::size_t>(size));
            check(controls.size() <= 1024);
        }
        for (auto newline = controls.find('\n'); newline != controls.npos; newline = controls.find('\n')) {
            const auto command = controls.substr(0, newline);
            controls.erase(0, newline + 1);
            if (command == "quit") quit = true;
            else if (command == "insert") insert();
            else if (command == "remove") remove();
            else if (command == "offline" || command == "online") {
                offline = command == "offline";
                enrichment.stop_background();
            } else if (command == "hold") {
                hold_images = true;
                image_started = false;
                enrichment.stop_background();
            } else if (command == "release") hold_images = false;
            else throw std::runtime_error("unknown test-fixture stdin control");
        }
        enrichment.poll();
        refresh();
        server.service();
        const std::array<unsigned, 4> counts{metadata_calls, cover_calls, background_calls, image_calls};
        if (counts != previous_counts) {
            emit_fixture({{"fixture_counts", {{"metadata", counts[0]}, {"cover", counts[1]},
                {"background", counts[2]}, {"image", counts[3]}}}});
            previous_counts = counts;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    enrichment.stop_background();
    return 0;
}
}
#endif

int main(int argc, char** argv) {
    try {
#ifdef ENABLE_API
        if (argc == 4 && std::string_view(argv[1]) == "--serve-fixture")
            return serve_fixture(std::stoi(argv[2]), argv[3]);
#else
        (void)argv;
#endif
        check(argc == 1);
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
        {
            ArtistBackgroundPolicy policy(true);
            EnrichmentService service(true, {}, policy, "synthetic-project-key");
            service.begin_if_needed(toc);
            await_metadata(service);
            check(!policy.effective() && background_calls == 0 && image_calls == 0);
            check(policy.set_enabled(true) && policy.effective());
            await(service, [&] { return service.background_url().has_value(); });
            const auto first = *service.background_url();
            check(first.starts_with("/api/presentation/artwork/background/"));
            check(first.find("0383dadf") == first.npos && first.find("fanart") == first.npos);
            const auto asset = service.background_asset(first);
            check(asset && asset->mime_type == "image/jpeg" && asset->bytes == "synthetic-image-bytes");
            check(!service.background_asset(first + "-stale"));
#ifdef ENABLE_API
            const ApiStateProvider presentation = [&] {
                ReadDiagnostics read;
                read.session_id = "fixture-session";
                return serialize_presentation_model(make_presentation_model(1, {}, MediaLifecycleState::audio_ready,
                    toc, service.snapshot(), {}, read, {}, false, 1, service.generation(), service.background_url()));
            };
            const ApiStateProvider status = [&] { return policy.json(); };
            const ApiCommandHandler commands = [&](const ApiCommand& command) {
                if (command.type != ApiCommandType::set_artist_background || !policy.set_enabled(command.enabled)) return false;
                if (!command.enabled) service.stop_background();
                return true;
            };
            const ApiBackgroundArtworkProvider images = [&](std::string_view path) -> std::optional<ApiResponse> {
                const auto image = service.background_asset(path);
                if (!image) return std::nullopt;
                return ApiResponse{200, image->mime_type, image->bytes};
            };
            const auto route = [&](std::string_view method, std::string_view path, std::string_view body = {}) {
                return route_api_request(method, path, presentation, commands, body, {}, nullptr,
                                         {}, {}, {}, status, images);
            };
            const auto snapshot = route("GET", "/api/state");
            check(snapshot.status == 200 && nlohmann::json::parse(snapshot.body)["artwork"]["background"]["url"] == first);
            check(snapshot.body.find("0383dadf") == first.npos && snapshot.body.find("fanart.tv") == first.npos);
            check(route("GET", first).status == 200 && route("GET", first).body == asset->bytes);
            check(route("GET", first + "-stale").status == 404);
#endif
            const auto generation = service.generation();
            check(service.select_candidate(generation, 1));
            check(!service.background_asset(first));
            await(service, [&] { return service.background_url().has_value(); });
            const auto second = *service.background_url();
            check(second != first);
            check(policy.set_enabled(false));
            service.stop_background();
            check(!service.background_url() && !service.background_asset(second));
#ifdef ENABLE_API
            check(route("POST", "/api/artist-background", R"({"enabled":false})").status == 204);
            check(route("GET", second).status == 404);
            check(nlohmann::json::parse(route("GET", "/api/state").body)["artwork"]["background"].is_null());
#endif
            const auto off_calls = background_calls.load();
            const auto off_images = image_calls.load();
            for (unsigned i = 0; i < 20; ++i) service.poll();
            check(background_calls == off_calls && image_calls == off_images);

            hold_images = true;
            image_started = false;
            check(policy.set_enabled(true));
            await(service, [&] { return image_started.load(); });
            check(!service.background_url());
            check(policy.set_enabled(false));
            service.stop_background();
            hold_images = false;
            for (unsigned i = 0; i < 20; ++i) service.poll();
            check(!service.background_url());

            check(policy.set_enabled(true));
            await(service, [&] { return service.background_url().has_value(); });
            const auto before_decline = *service.background_url();
            check(service.decline_candidates(generation));
            check(!service.background_asset(before_decline));
            check(service.select_candidate(generation, 0));
            await(service, [&] { return service.background_url().has_value(); });
            const auto before_disc = *service.background_url();
            service.invalidate();
            check(!service.background_url() && !service.background_asset(before_disc));
            const auto no_disc_calls = background_calls.load();
            for (unsigned i = 0; i < 20; ++i) service.poll();
            check(policy.effective() && background_calls == no_disc_calls);
            offline = true;
            service.begin_if_needed(toc);
            await_metadata(service);
            await(service, [&] { return background_calls > no_disc_calls; });
            check(!service.background_url() && policy.effective());
            check(service.snapshot().status == MetadataStatus::available);
        }
        std::cout << "PASS: OFF zero requests; ON delivery, fallback, invalidation, cancellation and offline metadata independence\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
