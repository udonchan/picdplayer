#include "api_server.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>

namespace { void check(bool value) { if (!value) throw std::runtime_error("API route test failed"); } }
int main() {
    try {
        int calls = 0;
        const ApiStateProvider provider = [&] { ++calls; return std::string(R"({"revision":7})"); };
        auto response = route_api_request("GET", "/api/state", provider);
        check(response.status == 200 && response.content_type == "application/json");
        check(response.body == R"({"revision":7})" && calls == 1);
        response = route_api_request("POST", "/api/state", provider);
        check(response.status == 405 && calls == 1);
        response = route_api_request("GET", "/missing", provider);
        check(response.status == 404 && calls == 1);
        const ApiStateProvider huge = [] { return std::string(1024 * 1024 + 1, 'x'); };
        check(route_api_request("GET", "/api/state", huge).status == 500);

        ApiServer server("127.0.0.1", 0, provider);
        std::atomic<bool> done = false;
        std::string received;
        std::thread client([&] {
            const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd < 0) { done = true; return; }
            timeval timeout{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{}; address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(server.port()));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                constexpr char request[] = "GET /api/state HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
                (void)send(fd, request, std::strlen(request), 0);
                char buffer[2048];
                for (;;) { const auto count = recv(fd, buffer, sizeof(buffer), 0); if (count <= 0) break; received.append(buffer, static_cast<std::size_t>(count)); }
            }
            close(fd); done = true;
        });
        for (int i = 0; i < 1000 && !done; ++i) {
            server.service(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        client.join();
        check(received.find("200 OK") != std::string::npos);
        check(received.find(R"({"revision":7})") != std::string::npos);

        std::atomic<bool> got_initial_event = false;
        std::atomic<bool> websocket_done = false;
        std::string websocket_received;
        std::thread websocket_client([&] {
            const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd < 0) { websocket_done = true; return; }
            timeval timeout{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{}; address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(server.port()));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                constexpr char request[] =
                    "GET /api/events HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                    "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                    "Sec-WebSocket-Version: 13\r\n\r\n";
                (void)send(fd, request, std::strlen(request), 0);
                char buffer[2048];
                for (;;) {
                    const auto count = recv(fd, buffer, sizeof(buffer), 0);
                    if (count <= 0) break;
                    websocket_received.append(buffer, static_cast<std::size_t>(count));
                    if (websocket_received.find(R"({"revision":7})") != std::string::npos)
                        got_initial_event = true;
                    if (websocket_received.find(R"({"revision":8})") != std::string::npos) break;
                }
            }
            close(fd); websocket_done = true;
        });
        for (int i = 0; i < 1000 && !got_initial_event; ++i) {
            server.service(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(got_initial_event);
        server.publish_state(R"({"revision":8})");
        for (int i = 0; i < 1000 && !websocket_done; ++i) {
            server.service(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        websocket_client.join();
        check(websocket_received.find("101 Switching Protocols") != std::string::npos);
        check(websocket_received.find(R"({"revision":8})") != std::string::npos);
        std::cout << "PASS: HTTP state and WebSocket event API\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
