#include "presentation_model.hpp"
#include "presentation_json.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

namespace { void check(bool value) { if (!value) throw std::runtime_error("presentation model test failed"); } }

int main() {
    try {
        DiscToc toc{{{1, 150, 750}, {2, 900, 1500}}, 2400};
        PlayerState player{PlaybackState::stopped, 1, 150};
        MetadataResult metadata;
        metadata.status = MetadataStatus::available;
        metadata.selected = 0;
        DiscMetadata album;
        album.release_id = "provider-private";
        album.album_title = "Album";
        album.album_artist = "Artist";
        album.tracks.push_back({1, "Song", "Singer", "recording-private", {}});
        metadata.candidates.push_back({std::move(album)});

        const auto model = make_presentation_model(7, player, MediaLifecycleState::audio_ready,
                                                   toc, metadata, {}, {}, {});
        check(model.disc.title == "Album" && model.tracks.size() == 2);
        check(model.tracks[0].title == "Song" && !model.tracks[1].title);
        const auto json = nlohmann::json::parse(serialize_presentation_model(model));
        check(json["schema_version"] == 1 && json["player"]["track_number"] == 1);
        check(json["disc"]["title"] == "Album" && json["tracks"][0]["duration_frames"] == 750);
        const auto rendered = json.dump();
        check(rendered.find("provider-private") == std::string::npos &&
              rendered.find("recording-private") == std::string::npos);
        check(json["artwork"]["cover"].is_null());
        auto diagnostic_model = model;
        diagnostic_model.drive.vendor = "Test drive";
        diagnostic_model.read.stats.direct_retries = 7;
        diagnostic_model.read.stream_generation = 9;
        diagnostic_model.read.coverage.observe(ReadResult{10, 15, 15, ReadStatus::ok, 0, 0});
        ReadResult accepted{150, 15, 15, ReadStatus::ok, 0, 0};
        accepted.verification.attempts = 2;
        accepted.verification.complete_reads = 2;
        accepted.verification.matching_reads = 2;
        accepted.verification.detail_count = 2;
        accepted.verification.details[0] = {15, true, 0, 0, 1};
        accepted.verification.details[1] = {15, true, 0, 0, 1};
        accepted.verification.accepted_candidate = 1;
        accepted.verification.accepted_attempt = 2;
        diagnostic_model.read.latest = make_read_evidence(accepted);
        PlayerEvent event;
        event.sequence = 42;
        event.stream_generation = diagnostic_model.read.stream_generation;
        diagnostic_model.recent_events.push_back(event);
        const auto diagnostic = nlohmann::json::parse(serialize_presentation_model(diagnostic_model));
        check(diagnostic["drive"]["vendor"] == "Test drive");
        check(diagnostic["read"]["stats"]["direct_retries"] == 7);
        check(diagnostic["read"]["stream_generation"] == 9);
        check(diagnostic["read"]["coverage"]["accepted_unique_frames"] == 15);
        check(diagnostic["read"]["coverage"]["scope"] == "STREAM");
        check(diagnostic["recent_events"][0]["sequence"] == 42);
        check(!presentation_json_equal_ignoring_revision(rendered, diagnostic.dump()));
        check(!diagnostic.contains("metadata"));
        check(!diagnostic["read"]["history"].contains("regions"));
        auto detailed = diagnostic_model;
        detailed.read.history_included = true;
        detailed.read.recent_reads.push_back(*detailed.read.latest);
        const auto detail_json = nlohmann::json::parse(serialize_presentation_model(detailed));
        check(detail_json["read"]["history"]["regions"].size() == 1);
        check(detail_json["read"]["history"]["included"] == true);
        const auto& verification = diagnostic["read"]["latest"]["verification"];
        check(verification["attempt_details"][0]["candidate"] == 1);
        check(verification["accepted_candidate"] == 1);
        check(verification["detail_capacity"] == 8);
        auto single = model;
        single.read.latest = make_read_evidence(ReadResult{150, 15, 15, ReadStatus::ok, 0, 0});
        const auto single_json = nlohmann::json::parse(serialize_presentation_model(single));
        check(single_json["read"]["latest"]["verification"]["attempt_details"].empty());
        check(single_json["read"]["latest"]["verification"]["accepted_candidate"].is_null());
        ReadDiagnostics identity;
        identity.session_id = "session-a";
        const DiscToc unusual{{{3, 150, 750}, {4, 900, 1500}}, 2400};
        const auto layout_json = [&](MediaLifecycleState state, std::optional<DiscToc> disc,
                                     std::optional<std::uint64_t> generation) {
            return nlohmann::json::parse(serialize_presentation_model(make_presentation_model(
                8, player, state, disc, {}, {}, identity, {}, false, generation)));
        };
        auto exposed = layout_json(MediaLifecycleState::audio_ready, unusual, 2);
        check(exposed["disc"]["layout"]["start_lba"] == 150);
        check(exposed["disc"]["layout"]["leadout_lba"] == 2400);
        check(exposed["disc"]["layout"]["tracks"][0]["number"] == 3);
        check(exposed["disc"]["layout"]["tracks"][0]["end_lba"] == 900);
        check(exposed["disc"]["layout"]["tracks"][1]["end_lba"] == 2400);
        check(exposed["disc"]["layout"]["disc_generation"] == 2);
        check(exposed["disc"]["layout"]["session_id"] == "session-a");
        for (auto state : {MediaLifecycleState::no_disc, MediaLifecycleState::loading,
                           MediaLifecycleState::unsupported, MediaLifecycleState::ejecting,
                           MediaLifecycleState::eject_error})
            check(layout_json(state, unusual, 2)["disc"]["layout"].is_null());
        check(layout_json(MediaLifecycleState::audio_ready, unusual, std::nullopt)["disc"]["layout"].is_null());
        check(layout_json(MediaLifecycleState::audio_ready, std::nullopt, 2)["disc"]["layout"].is_null());
        check(layout_json(MediaLifecycleState::audio_ready, DiscToc{}, 2)["disc"]["layout"].is_null());
        check(layout_json(MediaLifecycleState::audio_ready, unusual, 3)["disc"]["layout"]["disc_generation"] == 3);
        identity.session_id = "session-b";
        check(layout_json(MediaLifecycleState::audio_ready, unusual, 2)["disc"]["layout"]["session_id"] == "session-b");
        identity.session_id.clear();
        check(layout_json(MediaLifecycleState::audio_ready, unusual, 2)["disc"]["layout"].is_null());
        std::cout << "PASS: provider-neutral presentation model\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
