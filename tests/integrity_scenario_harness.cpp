#include "api_server.hpp"
#include "diagnostics_json.hpp"
#include "integrity_fixture.hpp"
#include "playback_engine.hpp"
#include "presentation_json.hpp"
#include "presentation_model.hpp"

#include <csignal>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }

struct SilentOutput final : AudioOutput {
    explicit SilentOutput(bool consume) : consume_(consume) {}
    void reset() override {}
    std::size_t write(std::span<const std::int16_t> samples) override {
        return consume_ ? samples.size() / 2 : 0;
    }
    std::int64_t delay() override { return consume_ ? 15 * (cdda_samples_per_frame / 2) : 0; }
    bool drain() override { return true; }
private:
    bool consume_;
};

ScriptedRead read(std::int32_t lba, unsigned retry = 0, ParanoiaEvents events = {},
                  LocalReadVerification verification = {}) {
    ReadResult result{lba, 15, 15, ReadStatus::ok, 0, retry};
    result.paranoia = events;
    result.verification = verification;
    return {result, 1};
}

std::vector<ScriptedRead> scenario(std::string_view name) {
    std::vector<ScriptedRead> result;
    LocalReadVerification mismatch;
    mismatch.attempts = 3; mismatch.complete_reads = 3; mismatch.matching_reads = 2;
    mismatch.mismatches = 1; mismatch.accepted_candidate = 2; mismatch.accepted_attempt = 3;
    for (int index = 0; index < 120; ++index) {
        const auto lba = index * 15;
        if (name == "clean" || name == "read-ahead") result.push_back(read(lba));
        else if (name == "retry") result.push_back(read(lba, 1));
        else if (name == "recovered") result.push_back(read(lba, 0, {0, 1, 1}));
        else if (name == "uncertain") {
            if (index == 2) {
                ReadResult failed{lba, 15, 0, ReadStatus::read_error, EIO, 1};
                result.push_back({failed, 0});
                break;
            }
            result.push_back(read(lba));
        }
        else if (name == "transition") {
            if (index == 1) result.push_back(read(lba, 1));
            else if (index >= 2) result.push_back(read(lba, 0, {}, mismatch));
            else result.push_back(read(lba));
        } else if (name == "mixed") {
            if (index == 1) result.push_back(read(lba, 1));
            else if (index >= 2) result.push_back(read(lba, 0, {0, 1, 1}));
            else result.push_back(read(lba));
        } else throw std::invalid_argument("unknown scenario: " + std::string(name));
    }
    return result;
}

void usage() {
    std::cerr << "usage: integrity_scenario_harness --scenario clean|retry|recovered|uncertain|mixed|read-ahead|transition"
              << " [--port 0..65535] [--read-delay-ms 1..1000]\n";
}
}

int main(int argc, char** argv) {
    try {
        std::string name = "mixed";
        int port = 0;
        unsigned delay_ms = 250;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if ((argument == "--scenario" || argument == "--port" || argument == "--read-delay-ms") && index + 1 < argc) {
                const std::string value = argv[++index];
                if (argument == "--scenario") name = value;
                else if (argument == "--port") port = std::stoi(value);
                else delay_ms = static_cast<unsigned>(std::stoul(value));
            } else { usage(); return 2; }
        }
        if (port < 0 || port > 65535 || !delay_ms || delay_ms > 1000) { usage(); return 2; }

        const auto script = scenario(name);
        PcmWorker worker([script, delay_ms] {
            return std::make_unique<ScriptedCddaReader>(script, std::chrono::milliseconds(delay_ms));
        }, "fixture", {750, 45});
        worker.set_disc_generation(1);
        PlayerController controller;
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 1800);
        controller.load_disc(toc);
        // Keep read-ahead data queued so the UI can contrast latest read evidence
        // with the absence of submitted playback PCM.  Other scenarios consume
        // fixture PCM through the ordinary PlaybackEngine path.
        SilentOutput output(name != "read-ahead");
        PlaybackEngine engine(controller, worker, output, toc.leadout_lba);
        DriveCapabilities drive;
        drive.device = "fixture"; drive.vendor = "PiCDPlayer"; drive.model = "ScriptedCddaReader";
        std::vector<PlayerEvent> events;
        std::uint64_t event_sequence = 0, revision = 0;
        const std::string session = "fixture-" + name;
        std::string snapshot;
        const auto publish = [&] {
            PlayerEvent event;
            while (worker.pop_event(event)) {
                event.sequence = ++event_sequence;
                if (events.size() == 64) events.erase(events.begin());
                events.push_back(std::move(event));
            }
            auto diagnostics = engine.read_diagnostics();
            diagnostics.session_id = session;
            snapshot = serialize_presentation_model(make_presentation_model(++revision, controller.state(),
                MediaLifecycleState::audio_ready, toc, {}, drive, std::move(diagnostics), events, false, 1));
        };
        publish();
        ApiServer api("127.0.0.1", port, [&] { return snapshot; }, {}, {}, {}, {}, [&] {
            auto diagnostics = engine.read_diagnostics(true);
            diagnostics.session_id = session;
            auto fields = diagnostic_fields(drive, diagnostics, {})["read"];
            return nlohmann::json{{"schema_version", 1}, {"session_id", session},
                {"stream_generation", diagnostics.stream_generation}, {"history", fields["history"]},
                {"disc_map", fields.contains("disc_map") ? fields["disc_map"] : nlohmann::json(nullptr)}}.dump();
        });
        controller.play();
        engine.synchronize();
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        std::cout << "INTEGRITY_SCENARIO_HARNESS development_only=1 scenario=" << name << '\n';
        std::cout << "HARNESS_URL=http://127.0.0.1:" << api.port() << "/player\n" << std::flush;
        while (!stopping) {
            // A read error is intentionally left observable in the worker state;
            // PlaybackEngine normally converts it into a stopped session.
            if (name != "uncertain") engine.tick();
            publish();
            api.publish_state(snapshot);
            api.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        controller.stop(); engine.synchronize();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
