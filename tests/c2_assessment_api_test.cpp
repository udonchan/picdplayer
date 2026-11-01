#include "c2_assessment_session.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("C2 API invariant failed"); }
template<class F> void wait(F done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= end) throw std::runtime_error("C2 API wait timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}

int main() {
    try {
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
        std::atomic<unsigned> calls{0};
        MediaWorker worker(std::make_shared<DriveAccessCoordinator>(), [] { return MediaObservation::audio_disc; },
            [toc] { return toc; }, [] {}, {}, {}, {}, {}, {},
            [&](const C2Reference& reference, const auto& current) {
                check(reference.expected_toc && reference.expected_toc->leadout_lba == toc.leadout_lba);
                check(current()); ++calls;
                return C2AssessmentRunResult{C2AssessmentRunState::complete,
                    {Knowledge::yes, CapabilityEvidenceSource::tested, "conditional synthetic API test"}, {}, {}};
            });
        worker.invalidate(1);
        C2AssessmentSession session("session", worker);
        C2AssessmentContext base;
        base.input.gate = {true, true, true, true, true};
        base.input.toc = &toc; base.input.device_current = true;
        base.input.disc_generation = 2; base.input.observation_generation = 1; base.input.policy_revision = 3;
        base.input.offset_samples = 0; base.direct = base.c2_supported = base.capture_idle = true;
        const auto context = [&] { auto c = base; c.input.media_idle = worker.idle(); return c; };
        const ApiCommandHandler handler = [&](const ApiCommand& command) {
            return command.type == ApiCommandType::start_c2_assessment ? session.start(command, context()) : session.cancel(command);
        };
        const auto route = [&](std::string_view method, const std::string& body = {}) {
            return route_api_request(method, "/api/c2-assessment", [] { return "{}"; }, handler, body, {}, nullptr,
                {}, {}, {}, {}, {}, {}, {}, [&] { return session.response(context()); });
        };
        check(route("GET").status == 200 && calls == 0);
        check(route("PUT").status == 405 && calls == 0);
        check(route("POST", std::string(4097, 'x')).status == 413);
        nlohmann::json command = {{"action", "start"}, {"session_id", "session"}, {"disc_generation", 2},
            {"observation_generation", 1}, {"profile", c2_assessment_profile}, {"reference", {
                {"id", "operator-reference"}, {"provenance", "independent synthetic source"},
                {"independent", true}, {"alignment_verified", true}, {"offset_samples", 0},
                {"sectors", nlohmann::json::array({{{"lba", 10}, {"sha256", std::string(64, 'a')}}})}}}};
        for (unsigned variant = 0; variant < 11; ++variant) {
            auto invalid = command;
            switch (variant) {
            case 0: invalid["unknown"] = true; break;
            case 1: invalid["profile"] = "not-a-profile"; break;
            case 2: invalid["disc_generation"] = -1; break;
            case 3: invalid["reference"]["independent"] = false; break;
            case 4: invalid["reference"]["alignment_verified"] = false; break;
            case 5: invalid["reference"]["offset_samples"] = nullptr; break;
            case 6: invalid["reference"]["sectors"][0]["lba"] = 2147483648ULL; break;
            case 7: invalid["reference"]["sectors"][0]["extra"] = 1; break;
            case 8: invalid["reference"]["sectors"][0]["sha256"] = "invalid"; break;
            case 9: invalid["reference"]["id"] = "invalid id"; break;
            case 10: invalid["reference"]["expected_toc"] = {}; break;
            }
            check(route("POST", invalid.dump()).status == 400 && calls == 0 && worker.idle());
        }
        auto duplicate = command.dump();
        duplicate.insert(1, "\"session_id\":\"session\",");
        check(route("POST", duplicate).status == 400 && calls == 0);
        duplicate = command.dump();
        const auto hash = duplicate.find("\"sha256\":");
        check(hash != std::string::npos);
        duplicate.insert(hash, "\"sha256\":\"" + std::string(64, 'a') + "\",");
        check(route("POST", duplicate).status == 400 && calls == 0);
        base.input.offset_samples.reset();
        check(route("POST", command.dump()).status == 409 && calls == 0 && worker.idle());
        base.input.offset_samples = 0;
        auto stale = command; stale["observation_generation"] = 2;
        check(route("POST", stale.dump()).status == 409 && calls == 0);
        check(route("POST", command.dump()).status == 202);
        const auto check_private = [&] {
            const auto body = route("GET").body;
            check(body.find(std::string(64, 'a')) == std::string::npos);
            check(body.find("independent synthetic source") == std::string::npos);
            check(body.find("\"sha256\"") == std::string::npos);
            check(body.find("\"provenance\"") == std::string::npos);
        };
        check_private();
        MediaWorkerResult result{};
        wait([&] { return worker.pop(result); });
        session.offer(result);
        check(nlohmann::json::parse(route("GET").body)["value"] == "UNKNOWN");
        session.poll(context());
        check(nlohmann::json::parse(route("GET").body)["value"] == "YES" && calls == 1);
        check_private();
        const auto cancel = nlohmann::json{{"action", "cancel"}, {"session_id", "session"}, {"job_id", 1}}.dump();
        check(route("POST", cancel).status == 202);
        check(nlohmann::json::parse(route("GET").body)["value"] == "UNKNOWN");
        check_private();
        std::cout << "PASS: bounded explicit C2 API reference/context admission and receipt revocation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
