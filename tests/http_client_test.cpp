#include "http_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value) {
    if (!value) throw std::runtime_error("HTTP client test failed");
}

std::uint16_t closed_loopback_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("cannot create test socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        throw std::runtime_error("cannot bind test socket");
    }
    socklen_t size = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        ::close(fd);
        throw std::runtime_error("cannot inspect test socket");
    }
    ::close(fd);
    return ntohs(address.sin_port);
}
}

int main(int argc, char** argv) {
    try {
        HttpClient client;
        bool connection_failure = false;
        try {
            const auto port = closed_loopback_port();
            (void)client.get("https://127.0.0.1:" + std::to_string(port) + "/", 1024);
        } catch (const std::runtime_error& error) {
            connection_failure = std::string_view(error.what()).starts_with("HTTP request failed:");
        }
        check(connection_failure);
        std::cout << "PASS: deterministic HTTPS connection failure\n";
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
        check(argc == 1 || argc == 3);
        if (argc == 3) {
            const auto port = std::stoi(argv[1]);
            const std::string origin = "https://coverartarchive.org:" + std::to_string(port);
            HttpClient::TestTransport transport{
                .ca_file = argv[2],
                .resolve = {"coverartarchive.org:" + std::to_string(port) + ":127.0.0.1",
                            "archive.org:" + std::to_string(port) + ":127.0.0.1"},
                .allow_private_peer = true};
            HttpClient fixture(transport);
            const auto okay = fixture.get(origin + "/ok", 1024, {}, RedirectPolicy::follow_cover_art_archive);
            check(okay.status == 200 && okay.content_type.starts_with("application/json") &&
                  okay.body == R"({"images":[]})");
            const auto busy = fixture.get(origin + "/retry", 1024, {}, RedirectPolicy::follow_cover_art_archive);
            check(busy.status == 429 && busy.retry_after_seconds == 4);
            const auto followed = fixture.get(origin + "/redirect-ok", 1024, {}, RedirectPolicy::follow_cover_art_archive);
            check(followed.status == 200 && followed.body == R"({"images":[]})");
            bool rejected_redirect = false;
            try { (void)fixture.get(origin + "/redirect-bad", 1024, {}, RedirectPolicy::follow_cover_art_archive); }
            catch (const std::runtime_error& error) {
                rejected_redirect = std::string_view(error.what()) == "Cover Art redirect host is not allowed";
            }
            check(rejected_redirect);
            bool bounded = false;
            try { (void)fixture.get(origin + "/large", 16, {}, RedirectPolicy::follow_cover_art_archive); }
            catch (const std::runtime_error& error) {
                bounded = std::string_view(error.what()) == "HTTP response exceeds size limit";
            }
            check(bounded);
            transport.timeout_ms = 100;
            HttpClient short_timeout(transport);
            bool timed_out = false;
            try { (void)short_timeout.get(origin + "/slow", 1024, {}, RedirectPolicy::follow_cover_art_archive); }
            catch (const std::runtime_error& error) {
                timed_out = std::string_view(error.what()).find("Timeout") != std::string_view::npos ||
                            std::string_view(error.what()).find("timed out") != std::string_view::npos;
            }
            check(timed_out);
            transport.allow_private_peer = false;
            HttpClient protected_client(transport);
            bool rejected_private = false;
            try { (void)protected_client.get(origin + "/ok", 1024, {}, RedirectPolicy::follow_cover_art_archive); }
            catch (const std::runtime_error& error) {
                rejected_private = std::string_view(error.what()).starts_with("HTTP request failed:");
            }
            check(rejected_private);
        }
#else
        (void)argc; (void)argv;
#endif
        if (argc == 3)
            std::cout << "PASS: HTTPS fixture covers retry, redirect, bounds, timeout, and private peer policy\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
