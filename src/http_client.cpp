#include "http_client.hpp"
#include "http_security_policy.hpp"
#include <curl/curl.h>
#include <sys/socket.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
std::once_flag curl_once;
using FanartClock = std::chrono::steady_clock;
struct FanartGate {
    std::mutex mutex;
    FanartClock::time_point next_allowed{};
};
FanartGate fanart_api_gate, fanart_asset_gate;
void initialize_curl() {
    std::call_once(curl_once, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("curl initialization failed");
    });
}
struct WriteTarget { std::string body; std::size_t maximum; bool exceeded = false; };
struct HeaderTarget {
    bool check_redirect = false;
    bool rejected = false;
    std::optional<long long> retry_after;
};
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
    auto& target = *static_cast<HeaderTarget*>(opaque);
    try {
        std::string_view line(data, bytes);
        if (line.starts_with("HTTP/")) { target.retry_after.reset(); return bytes; }
        const auto begins = [&](std::string_view prefix) {
            return line.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), line.begin(),
                [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); });
        };
        const bool retry = begins("Retry-After:");
        if (!retry && (!target.check_redirect || !begins("Location:"))) return bytes;
        const std::string_view prefix = retry ? "Retry-After:" : "Location:";
        line.remove_prefix(prefix.size());
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1);
        if (retry) {
            // libcurl may discard huge delta-seconds while converting to a date.
            // Preserve the server's interval without overflowing our clock.
            if (!line.empty() && std::all_of(line.begin(), line.end(), [](unsigned char c) {
                    return c >= '0' && c <= '9';
                })) {
                long long seconds = 0;
                const auto parsed = std::from_chars(line.data(), line.data() + line.size(), seconds);
                if (parsed.ec == std::errc::result_out_of_range) seconds = std::numeric_limits<long long>::max();
                target.retry_after = seconds;
            }
        } else if (!is_allowed_cover_art_redirect(line)) { target.rejected = true; return 0; }
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
                             RedirectPolicy redirects, std::string_view api_key) const {
    if (!url.starts_with("https://")) throw std::invalid_argument("HTTP URL must use HTTPS");
    const bool follow_cover_art = redirects == RedirectPolicy::follow_cover_art_archive;
    const bool fanart_asset = redirects == RedirectPolicy::fanart_asset;
    if (!api_key.empty() && (redirects != RedirectPolicy::reject ||
        !url.starts_with("https://webservice.fanart.tv/v3.2/music/") ||
        url.find_first_of("?#") != std::string_view::npos))
        throw std::invalid_argument("API key is only allowed for fanart.tv music requests");
    if (follow_cover_art && !is_allowed_cover_art_url(url))
        throw std::invalid_argument("Cover Art URL host is not allowed");
    if (fanart_asset && (!url.starts_with("https://assets.fanart.tv/fanart/music/") ||
                         url.find_first_of("?#") != std::string_view::npos))
        throw std::invalid_argument("Artist background asset URL is not allowed");
    if (!api_key.empty() && (api_key.size() > 256 ||
        std::any_of(api_key.begin(), api_key.end(), [](unsigned char c) {
            return c <= 0x20 || c >= 0x7f;
        }))) throw std::invalid_argument("invalid API key");
    // Preserve separate API/CDN cooldowns across clients, artists and OFF/ON.
    // Hold through the transfer, but never wait for another request in that gate.
    auto* gate = !api_key.empty() ? &fanart_api_gate : fanart_asset ? &fanart_asset_gate : nullptr;
    std::unique_lock<std::mutex> fanart_lock;
    if (gate) {
        fanart_lock = std::unique_lock<std::mutex>(gate->mutex, std::defer_lock);
        if (!fanart_lock.try_lock())
            return HttpResponse{.status = 429, .content_type = {}, .body = {}, .retry_after_seconds = 1};
        const auto now = FanartClock::now();
        if (now < gate->next_allowed) {
            const auto remaining = gate->next_allowed - now;
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining);
            return HttpResponse{.status = 429, .content_type = {}, .body = {}, .retry_after_seconds =
                seconds.count() + (remaining > seconds ? 1 : 0)};
        }
    }
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl allocation failed");
    WriteTarget target{{}, maximum_bytes};
    const std::string owned_url(url);
    curl_easy_setopt(curl.get(), CURLOPT_URL, owned_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "PiCDPlayer/0.1.0 (https://github.com/udonchan/picdplayer)");
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    if (!api_key.empty()) {
        const std::string header = "api-key: " + std::string(api_key);
        headers.reset(curl_slist_append(nullptr, header.c_str()));
        if (!headers) throw std::runtime_error("HTTP header allocation failed");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    }
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
    HeaderTarget redirect_target{.check_redirect = follow_cover_art, .rejected = false, .retry_after = {}};
    if (follow_cover_art || fanart_asset || !api_key.empty()) {
        // Do not let a proxy bypass direct peer-address checks.
        curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
        curl_easy_setopt(curl.get(), CURLOPT_OPENSOCKETFUNCTION, open_public_socket);
        curl_easy_setopt(curl.get(), CURLOPT_OPENSOCKETDATA, &allow_private_peer);
    }
    if (follow_cover_art || gate) {
        curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, receive_header);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &redirect_target);
    }
    if (cancelled) {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, transfer_progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancelled);
    }
    const auto code = curl_easy_perform(curl.get());
    HttpResponse response;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    curl_off_t retry_after = -1;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RETRY_AFTER, &retry_after) == CURLE_OK && retry_after >= 0)
        response.retry_after_seconds = static_cast<long long>(retry_after);
    if (gate && redirect_target.retry_after) response.retry_after_seconds = redirect_target.retry_after;
    if (gate && response.status == 429) {
        // Missing/invalid/nonpositive Retry-After uses a conservative 60 seconds.
        // Never shorten a positive server delay, including on transfer failures.
        const auto delay = response.retry_after_seconds.value_or(60) > 0 ?
            response.retry_after_seconds.value_or(60) : 60;
        const auto now = FanartClock::now();
        const auto maximum_delay = std::chrono::duration_cast<std::chrono::seconds>(
            FanartClock::time_point::max() - now).count();
        gate->next_allowed = delay > maximum_delay ? FanartClock::time_point::max() :
            now + std::chrono::seconds(delay);
    }
    if (target.exceeded) throw std::runtime_error("HTTP response exceeds size limit");
    if (redirect_target.rejected) throw std::runtime_error("Cover Art redirect host is not allowed");
    if (code != CURLE_OK) {
        if (code == CURLE_ABORTED_BY_CALLBACK && cancelled && cancelled())
            throw std::runtime_error("HTTP request cancelled");
        throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
    }
    response.body = std::move(target.body);
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
