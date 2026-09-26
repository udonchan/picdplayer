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
        PlayerEvent event;
        event.sequence = 42;
        diagnostic_model.recent_events.push_back(event);
        const auto diagnostic = nlohmann::json::parse(serialize_presentation_model(diagnostic_model));
        check(diagnostic["drive"]["vendor"] == "Test drive");
        check(diagnostic["read"]["stats"]["direct_retries"] == 7);
        check(diagnostic["recent_events"][0]["sequence"] == 42);
        check(!presentation_json_equal_ignoring_revision(rendered, diagnostic.dump()));
        check(!diagnostic.contains("metadata"));
        std::cout << "PASS: provider-neutral presentation model\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
