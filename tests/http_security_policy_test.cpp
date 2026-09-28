#include "http_security_policy.hpp"

#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("HTTP security policy test failed"); }

sockaddr_in ipv4(const char* text) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, text, &address.sin_addr) != 1) throw std::runtime_error("invalid IPv4 test value");
    return address;
}

sockaddr_in6 ipv6(const char* text) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, text, &address.sin6_addr) != 1) throw std::runtime_error("invalid IPv6 test value");
    return address;
}
}

int main() {
    try {
        check(is_allowed_cover_art_url("https://coverartarchive.org/release/example/"));
        check(!is_allowed_cover_art_url("https://archive.org/download/example"));
        check(!is_allowed_cover_art_url("https://coverartarchive.org.evil.invalid/example"));
        check(!is_allowed_cover_art_url("https://coverartarchive.org@evil.invalid/example"));
        check(!is_allowed_cover_art_url("http://coverartarchive.org/example"));
        check(is_allowed_cover_art_redirect("https://archive.org/download/example"));
        check(is_allowed_cover_art_redirect("https://s3.us.archive.org/example"));
        check(is_allowed_cover_art_redirect("/same-origin/example"));
        check(!is_allowed_cover_art_redirect("//evil.invalid/example"));
        check(!is_allowed_cover_art_redirect("https://evil.invalid/example"));

        const auto public_v4 = ipv4("8.8.8.8");
        const auto private_v4 = ipv4("192.168.1.1");
        const auto link_local_v4 = ipv4("169.254.1.1");
        const auto loopback_v4 = ipv4("127.0.0.1");
        check(is_public_http_peer(reinterpret_cast<const sockaddr*>(&public_v4)));
        check(!is_public_http_peer(reinterpret_cast<const sockaddr*>(&private_v4)));
        check(!is_public_http_peer(reinterpret_cast<const sockaddr*>(&link_local_v4)));
        check(!is_public_http_peer(reinterpret_cast<const sockaddr*>(&loopback_v4)));
        const auto public_v6 = ipv6("2606:4700:4700::1111");
        const auto private_v6 = ipv6("fd00::1");
        const auto link_local_v6 = ipv6("fe80::1");
        check(is_public_http_peer(reinterpret_cast<const sockaddr*>(&public_v6)));
        check(!is_public_http_peer(reinterpret_cast<const sockaddr*>(&private_v6)));
        check(!is_public_http_peer(reinterpret_cast<const sockaddr*>(&link_local_v6)));
        std::cout << "PASS: cover-art redirect and peer-address restrictions\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
