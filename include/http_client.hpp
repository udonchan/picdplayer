#pragma once
#include <cstddef>
#include <optional>
#include <functional>
#include <string>
#include <string_view>
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
#include <vector>
#endif

struct HttpResponse {
    long status = 0;
    std::string content_type;
    std::string body;
    // Seconds from Retry-After when libcurl can parse the response header.
    std::optional<long long> retry_after_seconds;
};
enum class RedirectPolicy { reject, follow_cover_art_archive };

class HttpClient {
public:
    HttpClient();
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
    struct TestTransport {
        std::string ca_file;
        std::vector<std::string> resolve;
        bool allow_private_peer = false;
        long timeout_ms = 15000;
    };
    explicit HttpClient(TestTransport transport);
#endif
    HttpResponse get(std::string_view url, std::size_t maximum_bytes,
                     const std::function<bool()>& cancelled = {},
                     RedirectPolicy redirects = RedirectPolicy::reject) const;
    static std::string escape(std::string_view value);
#ifdef PICDPLAYER_HTTP_TEST_HOOKS
private:
    TestTransport test_transport_;
#endif
};
