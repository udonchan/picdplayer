#include "complete_pcm_sha256.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("local SHA-256 assertion failed: " + std::to_string(checks));
}
void rejects(const std::function<void()>& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    check(rejected);
}
std::string output(CompletePcmConsumer& consumer) {
    const auto bytes = consumer.finalize();
    return {bytes.begin(), bytes.end()};
}
auto bytes(std::string_view text) {
    return std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}
}

int main() {
    try {
        const auto& profile = complete_pcm_sha256_profile();
        check(profile.canonical == complete_pcm_canonical_profile && profile.algorithm == "sha256");
        check(profile.version == "local-pcm-sha256-v1" && profile.state_bytes < complete_pcm_consumer_limit);
        check(profile.maximum_result_bytes == 16 * 1024);
        // Algorithm vectors exercise the consumer, not a complete-disc receipt.
        auto empty = make_complete_pcm_sha256_consumer();
        check(output(*empty).find("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != std::string::npos);
        constexpr std::string_view vector = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        constexpr std::string_view expected = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
        auto whole = make_complete_pcm_sha256_consumer();
        whole->consume(7, bytes(vector));
        const auto result = output(*whole);
        check(result.find(expected) != std::string::npos);
        check(result.find("\"number\":7,\"bytes\":56") != std::string::npos);
        check(result.find("\"external_verification\":\"NOT_CHECKED\"") != std::string::npos);
        auto chunks = make_complete_pcm_sha256_consumer();
        for (std::size_t offset = 0; offset < vector.size(); offset += 4)
            chunks->consume(7, bytes(vector.substr(offset, 4)));
        check(output(*chunks) == result);
        rejects([&] { chunks->consume(7, bytes("abcd")); });
        rejects([&] { chunks->finalize(); });
        auto million = make_complete_pcm_sha256_consumer();
        const std::string block(1000, 'a');
        for (unsigned i = 0; i < 1000; ++i) million->consume(1, bytes(block));
        check(output(*million).find("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") != std::string::npos);

        auto bounded = make_complete_pcm_sha256_consumer();
        rejects([&] { bounded->consume(0, bytes("abcd")); });
        rejects([&] { bounded->consume(100, bytes("abcd")); });
        rejects([&] { bounded->consume(1, bytes("abc")); });
        const std::string oversized(complete_pcm_chunk_bytes + 4, 'a');
        rejects([&] { bounded->consume(1, bytes(oversized)); });
        for (int track = 1; track <= 99; ++track) bounded->consume(track, bytes("abcd"));
        const auto maximum = output(*bounded);
        check(maximum.size() <= profile.maximum_result_bytes);
        check(maximum.find("\"number\":99,\"bytes\":4") != std::string::npos);
        auto ordered = make_complete_pcm_sha256_consumer();
        ordered->consume(8, bytes("abcd"));
        rejects([&] { ordered->consume(7, bytes("abcd")); });
        ordered->discard();
        rejects([&] { ordered->consume(9, bytes("abcd")); });
        rejects([&] { ordered->finalize(); });

        CompletePcmPlan plan;
        plan.toc = make_audio_toc(7, std::vector<std::int32_t>{150, 151}, 152);
        plan.identity = {"sha256-test", 1, 1, 1, 0, 0, profile};
        plan.duration = std::chrono::seconds(10);
        CompletePcmInput core(plan, make_complete_pcm_sha256_consumer());
        check(core.reserved_bytes() < complete_pcm_memory_limit);
        std::vector<std::int16_t> pcm(2 * cdda_samples_per_frame);
        for (std::size_t i = 0; i < pcm.size(); i += 4) {
            pcm[i] = 0x1234; pcm[i + 1] = -2; pcm[i + 2] = -32768; pcm[i + 3] = 32767;
        }
        ReadResult read{150, 2, 2, ReadStatus::ok, 0, 0};
        read.offset = OffsetReadEvidence{0, PhysicalReadEvidence{150, 2, 150, 2, 0}};
        check(core.append(plan.identity, read, pcm));
        check(core.result().empty() && core.prepare(plan.identity) && core.result().empty());
        check(!core.confirm(plan.identity, plan.toc, false) && core.result().empty());
        check(core.confirm(plan.identity, plan.toc, true));
        const auto receipt = core.result();
        const std::string json(receipt.begin(), receipt.end());
        check(json.find("\"bytes\":4704,\"sha256\":\"28d1a98b027ecc2a0c40e1ae0f53d85973803a2fdcfb8c05adabe2db508d7c9e\"") != std::string::npos);
        const std::string track_hash = "8630587bf51e9013ceb2d919c94a4bc7999288d52dcc880a14203d56768d7d08";
        check(json.find("\"number\":7,\"bytes\":2352,\"sha256\":\"" + track_hash) != std::string::npos);
        check(json.find("\"number\":8,\"bytes\":2352,\"sha256\":\"" + track_hash) != std::string::npos);
        check(core.accepted_bytes() == 4704 && json.find("\"kind\":\"local_pcm_fingerprint\"") != std::string::npos);
        core.invalidate();
        check(core.result().empty());
        CompletePcmInput cancelled(plan, make_complete_pcm_sha256_consumer());
        check(cancelled.append(plan.identity, read, pcm) && cancelled.prepare(plan.identity));
        cancelled.cancel();
        check(cancelled.result().empty() && cancelled.state() == CompletePcmState::incomplete);
        std::cout << "PASS: local SHA-256 vectors, bounded result, canonical track/core receipts ("
                  << checks << " checks, state=" << profile.state_bytes << " bytes)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
