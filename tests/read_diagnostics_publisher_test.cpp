#include "read_diagnostics_publisher.hpp"
#include "api_server.hpp"
#include "diagnostics_json.hpp"
#include "playback_engine.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <source_location>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
void check(bool value, std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("diagnostic publisher check failed at " + std::to_string(where.line()));
}
template<class Predicate> void wait(Predicate predicate) {
    const auto end = Clock::now() + 4s;
    while (!predicate()) {
        check(Clock::now() < end);
        std::this_thread::sleep_for(1ms);
    }
}
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, open = false;
    void hold() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, 4s, [&] { return open; })) throw std::runtime_error("gate expired");
    }
    void reached() { std::unique_lock lock(mutex); check(changed.wait_for(lock, 2s, [&] { return entered; })); }
    void release() { std::lock_guard lock(mutex); open = true; changed.notify_all(); }
};
struct ReleaseGate { Gate& gate; ~ReleaseGate() { gate.release(); } };
ReadDiagnostics fixture(std::uint64_t stream = 1, bool detail = true) {
    ReadDiagnostics read;
    read.session_id = "synthetic-session";
    read.stream_generation = stream;
    read.history_included = detail;
    read.history_evicted = 17;
    read.disc_map = DiscReadMap{};
    read.disc_map->disc_generation = 1;
    read.disc_map->revision = 100;
    read.disc_map->size = DiscReadMap::capacity;
    for (std::size_t i = 0; i < DiscReadMap::capacity; ++i)
        read.disc_map->regions[i] = {static_cast<std::int64_t>(i * 15), static_cast<std::int64_t>((i + 1) * 15), 3};
    if (detail) for (std::size_t i = 0; i < read_history_capacity; ++i) {
        ReadEvidence evidence;
        evidence.disc_generation = 1;
        evidence.stream_generation = stream;
        evidence.read_sequence = i + 18;
        evidence.start_lba = i * 15;
        evidence.frames_requested = evidence.frames_read = 15;
        evidence.status = IntegrityReadStatus::clean;
        evidence.local_verification = LocalVerification::single_read;
        evidence.verification.detail_count = maximum_verification_attempts;
        read.recent_reads.push_back(evidence);
    }
    read.stats.read_calls = 145;
    return read;
}
class Reader final : public CddaReader {
    std::int32_t position_ = 0;
public:
    void seek(std::int32_t position) override { position_ = position; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        std::fill(pcm.begin(), pcm.end(), 0);
        const auto frames = pcm.size() / cdda_samples_per_frame;
        ReadResult result{position_, frames, frames, ReadStatus::ok, 0, 0};
        position_ += frames;
        return result;
    }
};
// Unlike an unlimited fake sink, this output consumes queued PCM with wall time
// and fails if the main owner misses its 100 ms refill deadline.
struct DeadlineOutput final : AudioOutput {
    bool running = false;
    double queued = 0;
    std::size_t accepted = 0;
    unsigned missed_deadlines = 0;
    Clock::time_point last;
    void consume() {
        const auto now = Clock::now();
        if (running) {
            queued -= std::chrono::duration<double>(now - last).count() * 44100;
            if (queued < -1) { ++missed_deadlines; throw AudioUnderrun("deadline output underrun"); }
        }
        last = now;
    }
    void reset() override { running = false; queued = 0; }
    std::size_t write(std::span<const std::int16_t> pcm) override {
        consume();
        const auto count = std::min(pcm.size() / 2, static_cast<std::size_t>(std::max(0.0, 4410 - queued)));
        queued += count; accepted += count;
        if (count) running = true;
        return count;
    }
    std::int64_t delay() override { consume(); return static_cast<std::int64_t>(queued); }
    bool drain() override { consume(); return queued < 1; }
};
struct Client {
    int fd;
    std::string response;
    explicit Client(int port, bool slow = false) : fd(socket(AF_INET, SOCK_STREAM, 0)) {
        check(fd >= 0);
        if (slow) {
            int receive_bytes = 1024;
            check(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_bytes, sizeof(receive_bytes)) == 0);
        }
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        const std::string request = "GET /api/read-history HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        check(send(fd, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size()));
    }
    ~Client() { close(fd); }
    bool receive() {
        char buffer[4096];
        for (;;) {
            const auto count = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (!count) return true;
            if (count < 0) { check(errno == EAGAIN || errno == EWOULDBLOCK); return false; }
            response.append(buffer, count);
        }
    }
};
}
int main() {
    try {
        {
            DeadlineOutput output;
            const std::vector<std::int16_t> pcm(4410 * 2);
            check(output.write(pcm) == 4410);
            std::this_thread::sleep_for(120ms);
            bool missed = false;
            try { (void)output.delay(); } catch (const AudioUnderrun&) { missed = true; }
            check(missed && output.missed_deadlines == 1);
        }
        {
            const auto input = fixture();
            const auto publication = serialize_read_diagnostics({input, 7, 123456, true});
            const auto history = nlohmann::json::parse(publication.history);
            const auto map = nlohmann::json::parse(publication.map);
            check(history["history"] == diagnostic_fields({}, input, {})["read"]["history"]);
            check(history["disc_map"] == map["disc_map"]);
            check(history["snapshot"]["sequence"] == 7 && history["snapshot"]["captured_at_unix_ms"] == 123456);
            check(history["history"]["regions"].size() == 128 && history["history"]["evicted"] == 17);
            check(!map.contains("history") && map["disc_map"]["regions"].size() == 256);
            check(serialize_read_diagnostics({fixture(1, false), 8, 123457, false}).history.empty());
        }
        {
            Gate held;
            std::atomic<unsigned> calls = 0;
            ReadDiagnosticsPublisher publisher([&](const DiagnosticCapture& capture) {
                if (++calls == 1) held.hold();
                return serialize_read_diagnostics(capture);
            });
            ReleaseGate release{held};
            unsigned captures = 0;
            std::uint64_t generation = 1;
            const auto capture = [&](bool detail) { ++captures; return fixture(generation, detail); };
            check(publisher.request({1, 1}, true, capture).empty());
            held.reached();
            for (generation = 2; generation <= 20; ++generation)
                check(publisher.request({generation, 1}, true, capture).empty());
            generation = 20;
            for (int i = 0; i < 1000; ++i) check(publisher.request({20, 1}, true, capture).empty());
            check(captures == 20 && calls == 1);
            held.release();
            std::string body;
            wait([&] { body = publisher.request({20, 1}, true, capture); return !body.empty(); });
            check(calls == 2 && captures == 20);
            check(nlohmann::json::parse(body)["stream_generation"] == 20);
            check(!publisher.request({20, 1}, false, capture).empty());
            check(publisher.request({20, 0}, true, capture).empty());
            wait([&] { body = publisher.request({20, 0}, true, capture); return !body.empty(); });
            check(nlohmann::json::parse(body)["disc_map"].is_null());
        }
        {
            unsigned captures = 0;
            ReadDiagnosticsPublisher publisher;
            const auto capture = [&](bool detail) { ++captures; check(!detail); return fixture(1, false); };
            std::string map;
            wait([&] { map = publisher.request({1, 1}, false, capture); return !map.empty(); });
            check(captures == 1 && !nlohmann::json::parse(map).contains("history"));
            bool rejected = false;
            try { publisher.request({2, 1}, true, [&](bool) { return fixture(1); }); }
            catch (const std::logic_error&) { rejected = true; }
            check(rejected);
        }
        {
            Gate first, second;
            std::atomic<unsigned> calls = 0;
            ReadDiagnosticsPublisher publisher([&](const DiagnosticCapture& capture) {
                if (++calls == 1) first.hold(); else second.hold();
                return serialize_read_diagnostics(capture);
            });
            ReleaseGate release_first{first}, release_second{second};
            const auto capture = [](bool detail) { return fixture(1, detail); };
            check(publisher.request({1, 1}, true, capture).empty());
            first.reached();
            check(publisher.request({1, 0}, true, capture).empty());
            check(publisher.request({1, 1}, true, capture).empty());
            first.release(); second.reached();
            check(publisher.request({1, 1}, true, capture).empty());
            second.release();
            std::string body;
            wait([&] { body = publisher.request({1, 1}, true, capture); return !body.empty(); });
            check(nlohmann::json::parse(body)["snapshot"]["sequence"] == 3);
        }
        {
            std::atomic<unsigned> calls = 0;
            ReadDiagnosticsPublisher publisher([&](const DiagnosticCapture&) -> DiagnosticPublication {
                ++calls; throw std::runtime_error("synthetic projection failure");
            });
            check(publisher.request({1, 1}, true, [](bool) { return fixture(); }).empty());
            wait([&] { return calls == 1; });
            for (int i = 0; i < 100; ++i)
                check(publisher.request({1, 1}, true, [](bool) { return fixture(); }).empty());
            check(calls == 1);
        }
        {
            std::atomic<unsigned> calls = 0;
            ReadDiagnosticsPublisher publisher([&](const DiagnosticCapture&) {
                ++calls;
                return DiagnosticPublication{std::string(1024 * 1024 + 1, 'x'), "{}"};
            });
            check(publisher.request({1, 1}, true, [](bool) { return fixture(); }).empty());
            wait([&] { return calls == 1; });
            check(publisher.request({1, 1}, true, [](bool) { return fixture(); }).empty());
        }
        {
            Gate held;
            std::atomic<unsigned> calls = 0;
            const auto owner = std::this_thread::get_id();
            ReadDiagnosticsPublisher publisher([&](const DiagnosticCapture& capture) {
                check(std::this_thread::get_id() != owner);
                ++calls;
                held.hold();
                return serialize_read_diagnostics(capture);
            });
            ReleaseGate release{held};
            PcmWorker worker([] { return std::make_unique<Reader>(); });
            worker.set_disc_generation(1);
            PlayerController controller;
            controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 75000));
            DeadlineOutput output;
            PlaybackEngine engine(controller, worker, output, 75000);
            controller.play(); engine.synchronize();
            const auto stream = worker.status().diagnostics.stream_generation;
            unsigned captures = 0;
            ApiServer api("127.0.0.1", 0, [] { return "{}"; }, {}, {}, {}, {}, [&] {
                return publisher.request({stream, 1}, true, [&](bool detail) {
                    ++captures;
                    check(std::this_thread::get_id() == owner);
                    return fixture(stream, detail);
                });
            });
            const auto tick = [&] {
                api.service();
                engine.tick();
                check(controller.state().playback == PlaybackState::playing);
                check(output.missed_deadlines == 0);
                std::this_thread::sleep_for(1ms);
            };
            wait([&] { tick(); return output.accepted > 4410; });
            const auto before = output.accepted;
            Client first(api.port());
            wait([&] { tick(); return first.receive(); });
            check(first.response.find("503 Service Unavailable") != std::string::npos);
            held.reached();
            const auto until = Clock::now() + 350ms;
            while (Clock::now() < until) tick();
            check(output.accepted > before + 4410 && calls == 1 && captures == 1);
            held.release();
            std::string ready;
            wait([&] {
                tick();
                ready = publisher.request({stream, 1}, true, [&](bool detail) { ++captures; return fixture(stream, detail); });
                return !ready.empty();
            });
            Client slow(api.port(), true);
            const auto before_slow = output.accepted;
            const auto slow_until = Clock::now() + 200ms;
            while (Clock::now() < slow_until) tick();
            check(output.accepted > before_slow + 4410);
            Client full(api.port());
            wait([&] { tick(); return full.receive(); });
            const auto separator = full.response.find("\r\n\r\n");
            check(separator != std::string::npos && full.response.find("200 OK") != std::string::npos);
            check(full.response.substr(separator + 4) == ready);
            check(nlohmann::json::parse(ready)["history"]["regions"].size() == 128);
            check(captures == 1 && calls == 1);
            controller.stop(); engine.synchronize();
        }
        std::cout << "PASS: bounded asynchronous history, generation rejection, and deadline audio with slow JSON/HTTP\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
