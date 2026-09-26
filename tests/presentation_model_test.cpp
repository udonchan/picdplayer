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
        const auto& verification = diagnostic["read"]["latest"]["verification"];
        check(verification["attempt_details"][0]["candidate"] == 1);
        check(verification["accepted_candidate"] == 1);
        check(verification["detail_capacity"] == 8);
        auto single = model;
        single.read.latest = make_read_evidence(ReadResult{150, 15, 15, ReadStatus::ok, 0, 0});
        const auto single_json = nlohmann::json::parse(serialize_presentation_model(single));
        check(single_json["read"]["latest"]["verification"]["attempt_details"].empty());
        check(single_json["read"]["latest"]["verification"]["accepted_candidate"].is_null());
        std::cout << "PASS: provider-neutral presentation model\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
