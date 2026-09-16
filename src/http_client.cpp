#include "http_client.hpp"
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {
std::once_flag curl_once;
struct WriteTarget { std::string body; std::size_t maximum; bool exceeded = false; };
size_t write_body(char* data, size_t size, size_t count, void* opaque) noexcept {
    auto& target = *static_cast<WriteTarget*>(opaque);
    const auto bytes = size * count;
    if (bytes > target.maximum - target.body.size()) { target.exceeded = true; return 0; }
    target.body.append(data, bytes); return bytes;
}
}
HttpClient::HttpClient() {
    std::call_once(curl_once, [] { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("curl initialization failed"); });
}
HttpResponse HttpClient::get(std::string_view url, std::size_t maximum_bytes) const {
    if (!url.starts_with("https://")) throw std::invalid_argument("HTTP URL must use HTTPS");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl allocation failed");
    WriteTarget target{{}, maximum_bytes};
    const std::string owned_url(url);
    curl_easy_setopt(curl.get(), CURLOPT_URL, owned_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "PiCDPlayer/0.1.0 (https://github.com/udonchan/picdplayer)");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 15000L);
    // JSON API endpoints must not redirect to an untrusted host. Artwork
    // binary download will use a separate allow-listed redirect policy.
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &target);
    const auto code = curl_easy_perform(curl.get());
    if (target.exceeded) throw std::runtime_error("HTTP response exceeds size limit");
    if (code != CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
    HttpResponse response; response.body = std::move(target.body);
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status);
    char* content_type = nullptr; curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &content_type);
    if (content_type) response.content_type = content_type;
    return response;
}
std::string HttpClient::escape(std::string_view value) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl allocation failed");
    char* escaped = curl_easy_escape(curl.get(), value.data(), static_cast<int>(value.size()));
    if (!escaped) throw std::runtime_error("URL escape failed");
    std::string result(escaped); curl_free(escaped); return result;
}
