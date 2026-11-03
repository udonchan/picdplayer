#include "api_server.hpp"
#include "artist_background_image_fixtures.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

#define check(value) do { if (!(value)) throw std::runtime_error("image HTTP test failed at line " + std::to_string(__LINE__)); } while (0)

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t mib = 1024 * 1024;
const std::string background = "/api/presentation/artwork/background/current";
const std::string cover = "/api/presentation/artwork/cover";

struct Client {
    int fd;
    std::string received;
    bool closed = false;

    explicit Client(int port, bool slow = false) : fd(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
        check(fd >= 0);
        // Keep a non-reading client's receive window small enough for backpressure.
        const int window = slow ? 4096 : 256 * 1024;
        check(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &window, sizeof(window)) == 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        check(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    }
    Client(const Client&) = delete;
    ~Client() { disconnect(); }
    void disconnect() { if (fd >= 0) { close(fd); fd = -1; } }
    void request(std::string_view path, bool keepalive = false, std::string_view method = "GET") {
        const auto text = std::string(method) + " " + std::string(path) +
            " HTTP/1.1\r\nHost: localhost\r\nConnection: " +
            (keepalive ? "keep-alive" : "close") + "\r\n\r\n";
        check(send(fd, text.data(), text.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(text.size()));
    }
    void read_available() {
        std::array<char, 65536> buffer{};
        for (;;) {
            const auto count = recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (count > 0) received.append(buffer.data(), static_cast<std::size_t>(count));
            else if (count == 0) { closed = true; return; }
            else if (errno == EINTR) continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            else if (errno == ECONNRESET) { closed = true; return; }
            else check(false);
        }
    }
    bool headers() const { return received.find("\r\n\r\n") != std::string::npos; }
    bool complete() const {
        const auto boundary = received.find("\r\n\r\n");
        if (boundary == std::string::npos) return false;
        const auto field = received.find("content-length:");
        check(field != std::string::npos && field < boundary);
        const auto line_end = received.find("\r\n", field);
        check(line_end != std::string::npos && line_end <= boundary);
        const auto size = std::stoull(received.substr(field + 15, line_end - field - 15));
        return received.size() >= boundary + 4 + size;
    }
    std::string body() const { check(headers()); return received.substr(received.find("\r\n\r\n") + 4); }
    bool status(int code) const { return received.starts_with("HTTP/1.1 " + std::to_string(code)); }
};

template<class Predicate>
void await(ApiServer& server, Predicate predicate, std::chrono::seconds limit = std::chrono::seconds(5)) {
    const auto deadline = Clock::now() + limit;
    while (!predicate()) {
        check(Clock::now() < deadline);
        server.service();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void finish(ApiServer& server, Client& client) {
    await(server, [&] { client.read_available(); return client.complete(); });
    server.service();
}

struct Fixture {
    bool valid = true;
    bool fail = false;
    bool missing = false;
    bool oversized = false;
    std::size_t bytes = 0;
    std::chrono::seconds provider_delay{};
    unsigned calls = 0;
    std::string payload = std::string(reinterpret_cast<const char*>(artist_background_image_fixtures::png_bytes),
        sizeof(artist_background_image_fixtures::png_bytes));
    ApiServer server;

    Fixture() : server("127.0.0.1", 0, [] { return std::string("{\"revision\":1}"); }, {}, {}, {},
        [this] { return asset(4 * mib); }, {}, {},
        [this](std::string_view) { return asset(8 * mib); }, {}, {}, {},
        [this](std::string_view path) { return valid && (path == background || path == cover); }) {}

    std::optional<ApiResponse> asset(std::size_t maximum) {
        ++calls;
        if (provider_delay.count()) std::this_thread::sleep_for(provider_delay);
        if (fail) throw std::runtime_error("synthetic provider failure");
        if (missing) return std::nullopt;
        // Large patterns test transport bounds, not image geometry or decoding.
        return ApiResponse{200, "image/png", oversized ? std::string(maximum + 1, 'x') :
            bytes ? std::string(bytes, 'x') : payload};
    }

    void get(std::string_view path, int status, std::string_view expected = {}) {
        Client client(server.port());
        client.request(path);
        finish(server, client);
        check(client.status(status));
        if (!expected.empty()) check(client.body() == expected);
    }

    void check_slots_recovered() {
        const auto previous = bytes;
        bytes = 8 * mib;
        Client first(server.port(), true), second(server.port(), true);
        first.request(background);
        second.request(background);
        await(server, [&] { first.read_available(); second.read_available(); return first.headers() && second.headers(); });
        check(first.status(200) && second.status(200));
        first.disconnect();
        second.disconnect();
        for (unsigned i = 0; i < 20; ++i) server.service();
        bytes = previous;
    }
};
}

int main() {
    try {
        std::signal(SIGPIPE, SIG_IGN);
        Fixture fixture;
        auto& server = fixture.server;
        std::cout << "image HTTP: normal PNG and keepalive" << std::endl;
        for (const auto& path : {background, cover}) {
            Client client(server.port());
            client.request(path, true);
            finish(server, client);
            check(client.status(200) && client.body() == fixture.payload);
            check(client.received.find("content-type: image/png") != std::string::npos);
            check(client.received.find("cache-control: no-store") != std::string::npos);
            client.received.clear();
            client.request(path, true);
            finish(server, client);
            check(client.status(200) && client.body() == fixture.payload);
        }

        // Both existing single-image maxima remain transportable, including many writes.
        for (const auto& [path, size] : {std::pair{background, 8 * mib}, std::pair{cover, 4 * mib}}) {
            std::cout << "image HTTP: maximum transport bytes=" << size << std::endl;
            fixture.bytes = size;
            Client client(server.port());
            client.request(path);
            const auto started = Clock::now();
            finish(server, client);
            check(client.status(200) && client.body() == std::string(size, 'x'));
            std::cout << "image HTTP: maximum transport complete seconds="
                      << std::chrono::duration<double>(Clock::now() - started).count() << std::endl;
        }

        std::cout << "image HTTP: admission and disconnect" << std::endl;
        fixture.bytes = 8 * mib;
        Client first(server.port(), true), second(server.port(), true);
        first.request(background);
        second.request(background);
        await(server, [&] { first.read_available(); second.read_available(); return first.headers() && second.headers(); });
        check(first.status(200) && second.status(200));
        const auto calls = fixture.calls;
        fixture.get(cover, 503, R"({"error":"artwork_busy"})");
        check(fixture.calls == calls);
        fixture.get(background + "-old", 404);
        Client wrong_method(server.port());
        wrong_method.request(background, false, "POST");
        finish(server, wrong_method);
        check(wrong_method.status(405) && fixture.calls == calls);
        fixture.get("/api/state", 200, R"({"revision":1})");

        // Disconnecting clients frees slots even while their bodies are unfinished.
        first.disconnect();
        second.disconnect();
        fixture.bytes = 0;
        // Process the peer closes before checking admission again.
        for (unsigned i = 0; i < 20; ++i) server.service();
        fixture.get(cover, 200, fixture.payload);

        // OFF/generation retirement is represented by the owner's validator, not a new worker.
        std::cout << "image HTTP: retirement and provider failures" << std::endl;
        fixture.bytes = 8 * mib;
        Client retired(server.port(), true);
        retired.request(background);
        await(server, [&] { retired.read_available(); return retired.headers(); });
        fixture.valid = false;
        await(server, [&] { retired.read_available(); return retired.closed; });
        check(!retired.complete());
        const auto retired_calls = fixture.calls;
        fixture.get(background, 404);
        check(fixture.calls == retired_calls);
        fixture.valid = true;
        fixture.bytes = 0;
        fixture.get(background, 200, fixture.payload);

        fixture.missing = true;
        fixture.get(background, 404);
        fixture.missing = false;
        fixture.oversized = true;
        fixture.get(background, 500, R"({"error":"artwork_too_large"})");
        fixture.get(cover, 500, R"({"error":"artwork_too_large"})");
        fixture.oversized = false;
        fixture.fail = true;
        {
            Client broken(server.port());
            broken.request(background);
            await(server, [&] { broken.read_available(); return broken.closed; });
        }
        fixture.fail = false;
        fixture.get(background, 200, fixture.payload);
        fixture.check_slots_recovered();

        // Use the real 30-second production deadline; no timing knobs or fake old CI.
        std::cout << "image HTTP: absolute deadline including provider delay" << std::endl;
        fixture.bytes = 8 * mib;
        Client slow(server.port(), true);
        fixture.provider_delay = std::chrono::seconds(4);
        const auto started = Clock::now();
        slow.request(background);
        await(server, [&] { slow.read_available(); return slow.headers(); }, std::chrono::seconds(10));
        fixture.provider_delay = std::chrono::seconds(0);
        while (Clock::now() < started + std::chrono::seconds(25)) {
            server.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Client other(server.port(), true);
        other.request(background);
        await(server, [&] { other.read_available(); return other.headers(); }, std::chrono::seconds(1));
        check(other.status(200));
        const auto deadline_calls = fixture.calls;
        Client deadline_rejected(server.port());
        deadline_rejected.request(background);
        await(server, [&] { deadline_rejected.read_available(); return deadline_rejected.complete(); }, std::chrono::seconds(1));
        check(deadline_rejected.status(503) && deadline_rejected.body() == R"({"error":"artwork_busy"})");
        check(fixture.calls == deadline_calls);
        while (Clock::now() < started + std::chrono::seconds(31)) {
            server.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        await(server, [&] { slow.read_available(); return slow.closed; }, std::chrono::seconds(1));
        check(!slow.complete());
        other.disconnect();
        for (unsigned i = 0; i < 20; ++i) server.service();
        fixture.bytes = 0;
        fixture.get(background, 200, fixture.payload);
        fixture.check_slots_recovered();

        // Destroying a server with outstanding responses must not retain their owners.
        std::cout << "image HTTP: outstanding response destruction" << std::endl;
        std::unique_ptr<Client> outstanding;
        {
            Fixture dying;
            dying.bytes = 8 * mib;
            outstanding = std::make_unique<Client>(dying.server.port(), true);
            outstanding->request(background);
            await(dying.server, [&] { outstanding->read_available(); return outstanding->headers(); });
        }
        const auto close_deadline = Clock::now() + std::chrono::seconds(5);
        while (!outstanding->closed && Clock::now() < close_deadline) {
            outstanding->read_available();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(outstanding->closed && !outstanding->complete());
        std::cout << "image HTTP: normal/maxima/keepalive/admission/disconnect/retirement/failure/deadline passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
