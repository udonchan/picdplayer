#pragma once
#include <cstddef>
#include <optional>
#include <functional>
#include <string>
#include <string_view>

struct HttpResponse {
    long status = 0;
    std::string content_type;
    std::string body;
    // Seconds from Retry-After when libcurl can parse the response header.
    std::optional<long long> retry_after_seconds;
};
enum class RedirectPolicy { reject, follow_cover_art_archive, fanart_asset };

class HttpClient {
public:
    HttpClient();
    HttpResponse get(std::string_view url, std::size_t maximum_bytes,
                     const std::function<bool()>& cancelled = {},
                     RedirectPolicy redirects = RedirectPolicy::reject,
                     std::string_view api_key = {}) const;
    static std::string escape(std::string_view value);
};
