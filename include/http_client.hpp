#pragma once
#include <cstddef>
#include <string>
#include <string_view>

struct HttpResponse { long status = 0; std::string content_type; std::string body; };

class HttpClient {
public:
    HttpClient();
    HttpResponse get(std::string_view url, std::size_t maximum_bytes) const;
    static std::string escape(std::string_view value);
};
