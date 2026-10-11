#include "http_client.hpp"
#include "http_security_policy.hpp"
#include <curl/curl.h>
#include <sys/socket.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
std::once_flag curl_once;
void initialize_curl() {
    std::call_once(curl_once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("curl initialization failed");
    });
}
struct WriteTarget { std::string body; std::size_t maximum; bool exceeded = false; };
struct RedirectTarget { bool rejected = false; };
size_t write_body(char* data, size_t size, size_t count, void* opaque) noexcept {
    auto& target = *static_cast<WriteTarget*>(opaque);
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
        target.exceeded = true;
        return 0;
    }
    const auto bytes = size * count;
    if (bytes > target.maximum - target.body.size()) { target.exceeded = true; return 0; }
    // No exception may escape a C callback (or this noexcept function).
    try { target.body.append(data, bytes); return bytes; }
    catch (...) { return 0; }
}
int transfer_progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    const auto* cancelled = static_cast<const std::function<bool()>*>(opaque);
    try { return cancelled && *cancelled && (*cancelled)() ? 1 : 0; }
    catch (...) { return 1; }
}
size_t receive_header(char* data, size_t size, size_t count, void* opaque) noexcept {
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) return 0;
    const auto bytes = size * count;
    auto& target = *static_cast<RedirectTarget*>(opaque);
    try {
        std::string_view line(data, bytes);
        constexpr std::string_view prefix = "Location:";
        if (line.size() < prefix.size() || !std::equal(prefix.begin(), prefix.end(), line.begin(),
                                                        [](char left, char right) {
                                                            return std::tolower(static_cast<unsigned char>(left)) ==
                                                                   std::tolower(static_cast<unsigned char>(right));
                                                        })) return bytes;
        line.remove_prefix(prefix.size());
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
        if (!is_allowed_cover_art_redirect(line)) { target.rejected = true; return 0; }
        return bytes;
    } catch (...) { return 0; }
}
curl_socket_t open_public_socket(void* opaque, curlsocktype, curl_sockaddr* address) noexcept {
    const bool allow_private_peer = opaque && *static_cast<const bool*>(opaque);
    if (!address || (!allow_private_peer && !is_public_http_peer(&address->addr))) return CURL_SOCKET_BAD;
    return ::socket(address->family, address->socktype, address->protocol);
}
}
HttpClient::HttpClient() {
    initialize_curl();
}
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
HttpClient::HttpClient(TestTransport transport) : test_transport_(std::move(transport)) {
    initialize_curl();
}
#endif
HttpResponse HttpClient::get(std::string_view url, std::size_t maximum_bytes,
                             const std::function<bool()>& cancelled,
                             RedirectPolicy redirects) const {
    if (!url.starts_with("https://")) throw std::invalid_argument("HTTP URL must use HTTPS");
    const bool follow_cover_art = redirects == RedirectPolicy::follow_cover_art_archive;
    if (follow_cover_art && !is_allowed_cover_art_url(url))
        throw std::invalid_argument("Cover Art URL host is not allowed");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl allocation failed");
    WriteTarget target{{}, maximum_bytes};
    const std::string owned_url(url);
    curl_easy_setopt(curl.get(), CURLOPT_URL, owned_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "PiCDPlayer/0.1.0 (https://github.com/udonchan/picdplayer)");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    long timeout_ms = 15000L;
    bool allow_private_peer = false;
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
    timeout_ms = test_transport_.timeout_ms;
    allow_private_peer = test_transport_.allow_private_peer;
    if (!test_transport_.ca_file.empty())
        curl_easy_setopt(curl.get(), CURLOPT_CAINFO, test_transport_.ca_file.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolved(nullptr, curl_slist_free_all);
    for (const auto& entry : test_transport_.resolve) {
        auto* appended = curl_slist_append(resolved.get(), entry.c_str());
        if (!appended) throw std::runtime_error("test DNS override allocation failed");
        resolved.release();
        resolved.reset(appended);
    }
    if (resolved) curl_easy_setopt(curl.get(), CURLOPT_RESOLVE, resolved.get());
#endif
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, follow_cover_art ? 1L : 0L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &target);
    RedirectTarget redirect_target;
    if (follow_cover_art) {
        // Do not let a proxy bypass direct peer-address checks.
        curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
        curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, receive_header);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &redirect_target);
        curl_easy_setopt(curl.get(), CURLOPT_OPENSOCKETFUNCTION, open_public_socket);
        curl_easy_setopt(curl.get(), CURLOPT_OPENSOCKETDATA, &allow_private_peer);
    }
    if (cancelled) {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, transfer_progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancelled);
    }
    const auto code = curl_easy_perform(curl.get());
    if (target.exceeded) throw std::runtime_error("HTTP response exceeds size limit");
    if (redirect_target.rejected) throw std::runtime_error("Cover Art redirect host is not allowed");
    if (code != CURLE_OK) {
        if (code == CURLE_ABORTED_BY_CALLBACK && cancelled && cancelled())
            throw std::runtime_error("HTTP request cancelled");
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
    }
    HttpResponse response; response.body = std::move(target.body);
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    curl_off_t retry_after = -1;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RETRY_AFTER, &retry_after) == CURLE_OK && retry_after >= 0)
        response.retry_after_seconds = static_cast<long long>(retry_after);
    char* content_type = nullptr; curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &content_type);
    if (content_type) response.content_type = content_type;
    return response;
}
std::string HttpClient::escape(std::string_view value) {
    initialize_curl();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl allocation failed");
    char* escaped = curl_easy_escape(curl.get(), value.data(), static_cast<int>(value.size()));
    if (!escaped) throw std::runtime_error("URL escape failed");
    std::string result(escaped); curl_free(escaped); return result;
}
