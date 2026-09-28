#include "http_security_policy.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {
std::string normalized_host(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    while (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

bool archive_host(std::string_view host) {
    const auto value = normalized_host(host);
    return value == "archive.org" || value.ends_with(".archive.org");
}

bool allowed_url(std::string_view value, bool redirect) {
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, std::string(value).c_str(), 0) != CURLUE_OK) return false;
    char* scheme = nullptr;
    char* host = nullptr;
    char* user = nullptr;
    const auto valid = curl_url_get(url.get(), CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
                       curl_url_get(url.get(), CURLUPART_HOST, &host, 0) == CURLUE_OK &&
                       curl_url_get(url.get(), CURLUPART_USER, &user, 0) == CURLUE_NO_USER;
    std::unique_ptr<char, decltype(&curl_free)> scheme_owner(scheme, curl_free);
    std::unique_ptr<char, decltype(&curl_free)> host_owner(host, curl_free);
    std::unique_ptr<char, decltype(&curl_free)> user_owner(user, curl_free);
    if (!valid || std::string_view(scheme) != "https") return false;
    const auto normalized = normalized_host(host);
    return normalized == "coverartarchive.org" || (redirect && archive_host(normalized));
}

bool public_ipv4(const in_addr& address) {
    const auto value = ntohl(address.s_addr);
    const auto first = value >> 24;
    if (first == 0 || first == 10 || first == 127 || first >= 224) return false;
    if ((value & 0xffc00000U) == 0x64400000U) return false; // 100.64.0.0/10
    if ((value & 0xfff00000U) == 0xac100000U) return false; // 172.16.0.0/12
    if ((value & 0xffff0000U) == 0xa9fe0000U || (value & 0xffff0000U) == 0xc0a80000U) return false;
    if ((value & 0xfffe0000U) == 0xc6120000U) return false; // 198.18.0.0/15
    return true;
}

bool public_ipv6(const in6_addr& address) {
    if (IN6_IS_ADDR_UNSPECIFIED(&address) || IN6_IS_ADDR_LOOPBACK(&address) ||
        IN6_IS_ADDR_MULTICAST(&address) || IN6_IS_ADDR_LINKLOCAL(&address)) return false;
    if (IN6_IS_ADDR_V4MAPPED(&address)) {
        in_addr mapped{};
        std::copy_n(address.s6_addr + 12, 4, reinterpret_cast<unsigned char*>(&mapped));
        return public_ipv4(mapped);
    }
    return (address.s6_addr[0] & 0xfeU) != 0xfcU; // fc00::/7 unique local addresses
}
}

bool is_allowed_cover_art_url(std::string_view url) { return allowed_url(url, false); }

bool is_allowed_cover_art_redirect(std::string_view location) {
    if (location.starts_with('/') && !location.starts_with("//")) return true;
    return allowed_url(location, true);
}

bool is_public_http_peer(const sockaddr* address) {
    if (!address) return false;
    if (address->sa_family == AF_INET)
        return public_ipv4(reinterpret_cast<const sockaddr_in*>(address)->sin_addr);
    if (address->sa_family == AF_INET6)
        return public_ipv6(reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr);
    return false;
}
