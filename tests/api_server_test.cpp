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
        response = route_api_request("GET", "/debug/status", provider);
        check(response.status == 200 && response.content_type == "text/html; charset=utf-8");
        check(response.body.find("PiCDPlayer Technical Status") != std::string::npos);
        check(response.body.find("id=\"read-policy\"") != std::string::npos);
        check(response.body.find("/debug/status.js") != std::string::npos && calls == 1);
        response = route_api_request("GET", "/debug/status.js", provider);
        check(response.status == 200 && response.content_type == "text/javascript; charset=utf-8");
        check(response.body.find("/api/events") != std::string::npos);
        check(response.body.find("read.policy") != std::string::npos);
        check(response.body.find("(pending)") != std::string::npos);
        check(response.body.find("innerHTML") == std::string::npos);
        response = route_api_request("GET", "/debug/status.css", provider);
        check(response.status == 200 && response.content_type == "text/css; charset=utf-8");
        check(response.body.find("color-scheme:dark") != std::string::npos);
        check(route_api_request("POST", "/debug/status", provider).status == 405);
        response = route_api_request("GET", "/player", provider);
        check(response.status == 200 && response.content_type == "text/html; charset=utf-8");
        check(response.body.find("PiCDPlayer") != std::string::npos);
        check(response.body.find("/player.js") != std::string::npos && calls == 1);
        response = route_api_request("GET", "/player.js", provider);
        check(response.status == 200 && response.content_type == "text/javascript; charset=utf-8");
        check(response.body.find("/api/events") != std::string::npos);
        check(response.body.find("image.complete") != std::string::npos);
        check(response.body.find("innerHTML") == std::string::npos);
        response = route_api_request("GET", "/player.css", provider);
        check(response.status == 200 && response.content_type == "text/css; charset=utf-8");
        check(response.body.find("color-scheme:dark") != std::string::npos);
        check(route_api_request("POST", "/player", provider).status == 405);
        response = route_api_request("GET", "/missing", provider);
        check(response.status == 404 && calls == 1);
        ApiCommand received_command{ApiCommandType::play};
        int command_calls = 0;
        const ApiCommandHandler commands = [&](const ApiCommand& command) {
            received_command = command; ++command_calls; return true;
        };
        response = route_api_request("POST", "/api/next", provider, commands);
        check(response.status == 204 && response.body.empty());
        check(command_calls == 1 && received_command.type == ApiCommandType::next);
        check(route_api_request("GET", "/api/next", provider, commands).status == 405);
        check(route_api_request("POST", "/api/play", provider).status == 403);
        const ApiCommandHandler rejecting = [](const ApiCommand&) { return false; };
        check(route_api_request("POST", "/api/stop", provider, rejecting).status == 409);
        response = route_api_request("POST", "/api/seek", provider, commands,
                                     R"({"offset_seconds":-10})");
        check(response.status == 204 && received_command.type == ApiCommandType::seek_relative &&
              received_command.value == -10);
        response = route_api_request("POST", "/api/track", provider, commands, R"({"track":3})");
        check(response.status == 204 && received_command.type == ApiCommandType::select_track &&
              received_command.value == 3);
        check(route_api_request("POST", "/api/seek", provider, commands, "{}").status == 400);
        check(route_api_request("POST", "/api/track", provider, commands,
                                R"({"track":0})").status == 400);
        response = route_api_request("POST", "/api/eject", provider, commands);
        check(response.status == 202 && received_command.type == ApiCommandType::eject);
        const ApiStateProvider huge = [] { return std::string(1024 * 1024 + 1, 'x'); };
        check(route_api_request("GET", "/api/state", huge).status == 500);

        // A wildcard listener must still allow commands from a loopback peer.
        ApiServer server("0.0.0.0", 0, provider, commands);
        // No clients and no network traffic: the player must still make progress.
        const auto idle_start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) server.service();
        check(std::chrono::steady_clock::now() - idle_start < std::chrono::seconds(1));
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
        check(received.find("content-security-policy:") != std::string::npos);
        check(received.find("img-src 'self' data: https:") != std::string::npos);
        check(received.find("x-content-type-options: nosniff") != std::string::npos);

        done = false;
        received.clear();
        std::thread post_client([&] {
            const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd < 0) { done = true; return; }
            timeval timeout{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            sockaddr_in address{}; address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(server.port()));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                constexpr char request[] = "POST /api/track HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"track\":4}";
                (void)send(fd, request, std::strlen(request), 0);
                char buffer[2048];
                for (;;) { const auto count = recv(fd, buffer, sizeof(buffer), 0); if (count <= 0) break; received.append(buffer, static_cast<std::size_t>(count)); }
            }
            close(fd); done = true;
        });
        for (int i = 0; i < 1000 && !done; ++i) {
            server.service(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        post_client.join();
        check(received.find("HTTP/1.1 204") != std::string::npos);
        check(command_calls == 5 && received_command.type == ApiCommandType::select_track &&
              received_command.value == 4);

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

        const ApiReadPolicyProvider policy_provider = [] {
            return std::string(R"({"requested":{"mode":"REPEAT"},"effective":{"mode":"SINGLE"},"pending":true})");
        };
        response = route_api_request("GET", "/api/read-policy", provider, {}, {}, policy_provider);
        check(response.status == 200 && response.body.find("\"pending\":true") != std::string::npos);
        response = route_api_request("POST", "/api/read-policy", provider, commands,
            R"({"mode":"repeat","region_frames":75,"required_matches":2,"maximum_attempts":3,"time_budget_ms":10000})");
        check(response.status == 204 && received_command.type == ApiCommandType::set_read_policy);
        check(received_command.read_policy.mode == ReadVerificationMode::repeat &&
              received_command.read_policy.region_frames == 75 &&
              received_command.read_policy.required_matches == 2);
        check(route_api_request("POST", "/api/read-policy", provider, commands,
                                R"({"mode":"repeat","region_frames":14,"required_matches":2,"maximum_attempts":3,"time_budget_ms":10000})").status == 400);
        check(route_api_request("POST", "/api/read-policy", provider, commands, "{}").status == 400);
        std::cout << "PASS: HTTP state and WebSocket event API\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
