#include "http_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <chrono>
#include <future>
#include <atomic>
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
        bool key_scope_rejected = false;
        try { (void)client.get("https://example.com/", 1024, {}, RedirectPolicy::reject, "secret"); }
        catch (const std::invalid_argument&) { key_scope_rejected = true; }
        check(key_scope_rejected);
        bool key_redirect_rejected = false;
        try { (void)client.get("https://webservice.fanart.tv/v3.2/music/id", 1024, {},
                               RedirectPolicy::follow_cover_art_archive, "secret"); }
        catch (const std::invalid_argument&) { key_redirect_rejected = true; }
        check(key_redirect_rejected);
        bool key_query_rejected = false;
        try { (void)client.get("https://webservice.fanart.tv/v3.2/music/id?api_key=secret", 1024,
                               {}, RedirectPolicy::reject, "secret"); }
        catch (const std::invalid_argument&) { key_query_rejected = true; }
        check(key_query_rejected);
        std::cout << "PASS: deterministic HTTPS connection failure\n";
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
        if (argc == 4) {
            const std::string mode = argv[3];
            const std::string api = "https://webservice.fanart.tv/v3.2/music/";
            HttpClient::TestTransport transport{
                .ca_file = argv[2],
                .resolve = {"webservice.fanart.tv:443:127.0.0.1",
                            "assets.fanart.tv:443:127.0.0.1", "musicbrainz.org:443:127.0.0.1"},
                .allow_private_peer = true};
            HttpClient fixture(transport);
            const auto redirect = fixture.get(api + "redirect", 1024, {}, RedirectPolicy::reject, "fixture-project-key");
            check(redirect.status == 302 && redirect.body.empty());
            if (mode == "asset") {
                const std::string assets = "https://assets.fanart.tv/fanart/music/";
                const auto busy = fixture.get(assets + "first", 1024, {}, RedirectPolicy::fanart_asset);
                check(busy.status == 429 && busy.retry_after_seconds == 4);
                const auto blocked = HttpClient(transport).get(assets + "second", 1024, {}, RedirectPolicy::fanart_asset);
                check(blocked.status == 429 && blocked.body.empty() && blocked.content_type.empty() &&
                      blocked.retry_after_seconds && *blocked.retry_after_seconds > 0 && *blocked.retry_after_seconds <= 4);
                check(fixture.get(api + "first", 1024, {}, RedirectPolicy::reject, "fixture-project-key").status == 200);
                check(fixture.get("https://musicbrainz.org/ok", 1024).status == 200);
                std::cout << "PASS: independent CDN cooldown blocks next candidate without blocking API/metadata\n";
                return 0;
            }
            if (mode == "concurrent") {
                std::promise<void> started;
                std::atomic<bool> signalled = false;
                auto first = std::async(std::launch::async, [&] {
                    return fixture.get(api + "first", 1024, [&] {
                        if (!signalled.exchange(true)) started.set_value();
                        return false;
                    }, RedirectPolicy::reject, "fixture-project-key");
                });
                started.get_future().wait();
                const auto before = std::chrono::steady_clock::now();
                const auto blocked = HttpClient(transport).get(api + "second", 1024, {},
                    RedirectPolicy::reject, "fixture-project-key");
                check(blocked.status == 429 && blocked.body.empty() && blocked.content_type.empty() &&
                      blocked.retry_after_seconds == 1);
                check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100));
                check(first.get().status == 429);
            } else {
                const auto busy = fixture.get(api + "first", 1024, {}, RedirectPolicy::reject, "fixture-project-key");
                check(busy.status == 429);
                if (mode == "positive") check(busy.retry_after_seconds == 4);
                if (mode == "huge") check(busy.retry_after_seconds == 9223372036854775807LL);
            }
            // New client and artist simulate worker recreation after OFF/ON.
            const auto blocked = HttpClient(transport).get(api + "second", 1024, {},
                RedirectPolicy::reject, "fixture-project-key");
            check(blocked.status == 429 && blocked.body.empty() && blocked.content_type.empty());
            check(blocked.retry_after_seconds && *blocked.retry_after_seconds > 0);
            if (mode == "positive" || mode == "concurrent") check(*blocked.retry_after_seconds <= 4);
            else if (mode == "huge") check(*blocked.retry_after_seconds > 60);
            else check(*blocked.retry_after_seconds >= 59 && *blocked.retry_after_seconds <= 60);
            check(fixture.get(api + "unauthenticated", 1024).status == 200);
            check(fixture.get("https://assets.fanart.tv/fanart/music/image.jpg", 1024, {},
                              RedirectPolicy::fanart_asset).status == 200);
            check(fixture.get("https://musicbrainz.org/ok", 1024).status == 200);
            std::cout << "PASS: native fanart authentication and process cooldown " << mode << '\n';
            return 0;
        }
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
