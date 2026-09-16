#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

struct HttpResponse { long status = 0; std::string content_type; std::string body; };
enum class RedirectPolicy { reject, follow_https };

class HttpClient {
public:
    HttpClient();
    HttpResponse get(std::string_view url, std::size_t maximum_bytes,
                     const std::function<bool()>& cancelled = {},
                     RedirectPolicy redirects = RedirectPolicy::reject) const;
    static std::string escape(std::string_view value);
};
