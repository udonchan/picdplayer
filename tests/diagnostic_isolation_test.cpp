#include "api_server.hpp"
#include "logger.hpp"
#include <fstream>
#include <condition_variable>
#include <mutex>
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
// A deadline prevents a failed test from hanging the logger shutdown.
// 失敗時もlogger終了を無期限に待たせない。
struct HeldLog : std::streambuf {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, expired = false, fail = false;
    int overflow(int c) override {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(6), [&] { return released; })) {
            expired = true; released = true;
        }
        return fail ? traits_type::eof() : c;
    }
};
struct LogGuard {
    HeldLog buffer;
    std::streambuf* original = std::cout.rdbuf(&buffer);
    void release() {
        { std::lock_guard lock(buffer.mutex); buffer.released = true; }
        buffer.changed.notify_all();
        shutdown_logger();
        std::cout.rdbuf(original);
        std::cout.clear();
    }
    ~LogGuard() { release(); }
};
std::size_t resident_bytes() {
    std::ifstream input("/proc/self/statm");
    std::size_t total = 0, resident = 0;
    require(static_cast<bool>(input >> total >> resident));
    return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
}
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
        LogGuard logs;
        unsigned requests = 0;
        std::string snapshot = "{\"padding\":\"" + std::string(512 * 1024, 'x') + "\"}";
        ApiServer api("127.0.0.1", 0, [&] { ++requests; return snapshot; });
        api.publish_state(snapshot);
        Client http(api.port()), ws(api.port());
        http.send_request("GET /api/state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        ws.send_request("GET /api/events HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        PcmWorker worker([] { return std::make_unique<Reader>(); });
        worker.set_disc_generation(1);
        PlayerController controller;
        controller.load_disc(make_audio_toc(1, std::vector<std::int32_t>{0}, 1500000));
        Output output;
        PlaybackEngine engine(controller, worker, output, 1500000);
        controller.play(); engine.synchronize();
        // prebuffer_ready uses the real global playback logger.
        // 実際の再生ログを停止sinkへ流す。
        const auto log_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (;;) {
            require(std::chrono::steady_clock::now() < log_deadline);
            engine.tick();
            std::unique_lock lock(logs.buffer.mutex);
            if (logs.buffer.changed.wait_for(lock, std::chrono::milliseconds(1), [&] { return logs.buffer.entered; })) break;
        }
        const auto initial_audio = output.total;
        std::vector<std::unique_ptr<Client>> extra;
        for (int i = 0; i < 6; ++i) {
            auto client = std::make_unique<Client>(api.port());
            client->send_request("GET /api/events HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
            extra.push_back(std::move(client));
        }
        std::size_t warm_memory = 0, peak_memory = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        // Service network and audio on the same thread while neither client reads.
        // 両clientを非受信にし、daemonと同様にAPIとaudioを同一threadで進める。
        for (unsigned i = 0; i < 1200; ++i) {
            // Change every snapshot: identical publish calls are intentionally deduplicated.
            // 同一JSONは送信省略されるため、毎回異なる値を公開する。
            snapshot[20] = static_cast<char>('a' + i % 26);
            api.publish_state(snapshot);
            log_info("isolation") << "tick=" << i;
            api.service();
            engine.tick();
            require(std::chrono::steady_clock::now() < deadline);
            if (i == 200) warm_memory = resident_bytes();
            if (i >= 200) peak_memory = std::max(peak_memory, resident_bytes());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(requests > 0 && output.total > initial_audio + 588 * 300);
        require(global_logger().dropped() > 0);
        // Regression budget, not a proof of an absolute process memory bound.
        // 固定client数での増加回帰を検出する。任意接続数の総メモリ保証ではない。
        require(peak_memory <= warm_memory + 32 * 1024 * 1024);
        { std::lock_guard lock(logs.buffer.mutex); require(!logs.buffer.expired); }
        // Release as a failed write and keep ticking the real playback path.
        // 停止解除後は書込失敗に切り替え、再生経路を引き続き動かす。
        {
            std::lock_guard lock(logs.buffer.mutex);
            logs.buffer.fail = true;
            logs.buffer.released = true;
        }
        logs.buffer.changed.notify_all();
        shutdown_logger(); // Join proves the failed write has actually occurred.
        const bool failed_write = std::cout.bad();
        logs.release();
        const auto before_failure = output.total;
        for (int i = 0; i < 20; ++i) { api.service(); engine.tick(); }
        require(failed_write && output.total > before_failure);
        std::cout << "MEMORY_GROWTH=" << (peak_memory - warm_memory) << '\n';
        char response[256]{};
        const auto received = recv(ws.fd, response, sizeof(response), MSG_DONTWAIT);
        require(received > 0 && std::string(response, received).find("101 Switching Protocols") != std::string::npos);
        for (const auto& client : extra) {
            const auto count = recv(client->fd, response, sizeof(response), MSG_DONTWAIT);
            require(count > 0 && std::string(response, count).find("101 Switching Protocols") != std::string::npos);
        }
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
        payload["schema_version"] = 1;
        payload["revision"] = 1;
        payload["player"] = {{"state", "PLAYING"}};
        payload["disc"] = {{"state", "AUDIO_READY"}};
        std::cout << "DIAGNOSTIC_JSON=" << payload.dump() << '\n';
        const auto detailed = engine.read_diagnostics(true);
        require(detailed.disc_map && detailed.disc_map->size <= DiscReadMap::capacity);
        auto history_payload = diagnostic_fields({}, detailed, {})["read"]["history"];
        std::cout << "HISTORY_JSON=" << nlohmann::json{{"schema_version", 1},
            {"session_id", diagnostics.session_id}, {"stream_generation", detailed.stream_generation},
            {"history", history_payload}}.dump() << '\n';
        controller.stop(); engine.synchronize();
        std::cout << "PASS: stalled HTTP/WS clients preserve same-thread fake audio progress\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
