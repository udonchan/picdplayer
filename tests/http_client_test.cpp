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

int main() {
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
        std::cout << "PASS: HTTP client reports deterministic connection failure\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
