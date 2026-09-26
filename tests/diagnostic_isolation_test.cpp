#include "api_server.hpp"
#include "playback_engine.hpp"
#include "diagnostics_json.hpp"
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <source_location>

namespace {
void require(bool ok, std::source_location where = std::source_location::current()) { if (!ok) throw std::runtime_error("diagnostic isolation check failed at line " + std::to_string(where.line())); }
class Reader final : public CddaReader {
    std::int32_t position = 0;
public:
    void seek(std::int32_t value) override { position = value; }
    ReadResult read(std::span<std::int16_t> pcm) override {
        const auto frames = pcm.size() / cdda_samples_per_frame;
        std::fill(pcm.begin(), pcm.end(), 0);
        ReadResult result{position, frames, frames, ReadStatus::ok, 0, position == 0 ? 1u : 0u};
        position += frames;
        return result;
    }
};
struct Output final : AudioOutput {
    std::size_t total = 0;
    void reset() override {}
    std::size_t write(std::span<const std::int16_t> pcm) override {
        const auto frames = pcm.size() / 2;
        total += frames;
        return frames;
    }
    std::int64_t delay() override { return 0; }
    bool drain() override { return true; }
};
struct Client {
    int fd = -1;
    explicit Client(int port) {
        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        require(fd >= 0);
        int size = 1024;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
            close(fd); fd = -1; throw std::runtime_error("connect failed");
        }
    }
    ~Client() { if (fd >= 0) close(fd); }
    void send_request(const std::string& request) {
        require(send(fd, request.data(), request.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(request.size()));
    }
};
}
int main() {
    try {
        unsigned requests = 0;
        const std::string snapshot = "{\"padding\":\"" + std::string(512 * 1024, 'x') + "\"}";
        ApiServer api("127.0.0.1", 0, [&] { ++requests; return snapshot; });
        api.publish_state(snapshot);
        Client http(api.port()), ws(api.port());
        http.send_request("GET /api/state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        ws.send_request("GET /api/events HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        PcmWorker worker([] { return std::make_unique<Reader>(); });
        PlayerController controller;
        controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 150000));
        Output output;
        PlaybackEngine engine(controller, worker, output, 150000);
        controller.play(); engine.synchronize();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        // Service network and audio on the same thread while neither client reads.
        // 両clientを非受信にし、daemonと同様にAPIとaudioを同一threadで進める。
        for (unsigned i = 0; i < 300; ++i) {
            api.publish_state(snapshot);
            api.service();
            engine.tick();
            require(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(requests > 0 && output.total > 588 * 300);
        char response[256]{};
        const auto received = recv(ws.fd, response, sizeof(response), MSG_DONTWAIT);
        require(received > 0 && std::string(response, received).find("101 Switching Protocols") != std::string::npos);
        // The read proves upgrade occurred; the large body still cannot fit the receive window.
        // handshake成立を観測する。大きな本文をconsumerはまだ消費していない。
        const auto before = output.total;
        shutdown(http.fd, SHUT_RDWR); shutdown(ws.fd, SHUT_RDWR);
        for (int i = 0; i < 20; ++i) { api.service(); engine.tick(); }
        require(output.total > before);
        // Freeze production at the full PCM queue before inspecting the event window.
        // PCM queue満杯を同期点にし、event消費中の追加入力を除く。
        while (worker.status().queued != worker.buffer_capacity_blocks()) {
            require(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        auto diagnostics = engine.read_diagnostics();
        require(diagnostics.dropped_events > 0 && diagnostics.active_warning.has_value());
        diagnostics.session_id = "integration";
        std::vector<PlayerEvent> events;
        PlayerEvent event;
        while (worker.pop_event(event)) events.push_back(event);
        require(events.size() == read_event_capacity);
        events.erase(events.begin(), events.end() - 64);
        auto payload = diagnostic_fields({}, diagnostics, events);
        payload["revision"] = 1;
        payload["player"] = {{"state", "PLAYING"}};
        payload["disc"] = {{"state", "AUDIO_READY"}};
        std::cout << "DIAGNOSTIC_JSON=" << payload.dump() << '\n';
        controller.stop(); engine.synchronize();
        std::cout << "PASS: stalled HTTP/WS clients preserve same-thread fake audio progress\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
