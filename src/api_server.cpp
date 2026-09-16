#include "api_server.hpp"
#include <array>
#include <cstring>
#include <libwebsockets.h>
#include <stdexcept>
#include <utility>
#include <vector>

ApiResponse route_api_request(std::string_view method, std::string_view path,
                              const ApiStateProvider& state_provider) {
    if (path != "/api/state")
        return {HTTP_STATUS_NOT_FOUND, "application/json", R"({"error":"not_found"})"};
    if (method != "GET")
        return {HTTP_STATUS_METHOD_NOT_ALLOWED, "application/json", R"({"error":"method_not_allowed"})"};
    auto body = state_provider();
    constexpr std::size_t maximum_state_bytes = 1024 * 1024;
    if (body.size() > maximum_state_bytes)
        return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "application/json", R"({"error":"state_too_large"})"};
    return {HTTP_STATUS_OK, "application/json", std::move(body)};
}

struct ApiServer::Implementation {
    std::string address;
    int port;
    ApiStateProvider state_provider;
    std::string websocket_state;
    std::array<lws_protocols, 2> protocols{};
    lws_context* context = nullptr;

    static int callback(lws* wsi, lws_callback_reasons reason, void*, void* in, std::size_t length) noexcept {
        try {
            auto* self = static_cast<Implementation*>(lws_context_user(lws_get_context(wsi)));
            if (!self) return -1;
            if (reason == LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION) {
                std::array<char, 128> uri{};
                const auto count = lws_hdr_copy(wsi, uri.data(), uri.size(), WSI_TOKEN_GET_URI);
                return count > 0 && std::string_view(uri.data(), static_cast<std::size_t>(count)) == "/api/events"
                    ? 0 : 1;
            }
            if (reason == LWS_CALLBACK_ESTABLISHED) {
                lws_callback_on_writable(wsi);
                return 0;
            }
            if (reason == LWS_CALLBACK_SERVER_WRITEABLE) {
                constexpr std::size_t maximum_state_bytes = 1024 * 1024;
                if (self->websocket_state.size() > maximum_state_bytes) return -1;
                std::vector<unsigned char> message(LWS_PRE + self->websocket_state.size());
                std::memcpy(message.data() + LWS_PRE, self->websocket_state.data(), self->websocket_state.size());
                const auto written = lws_write(wsi, message.data() + LWS_PRE,
                                               self->websocket_state.size(), LWS_WRITE_TEXT);
                return written >= 0 && static_cast<std::size_t>(written) == self->websocket_state.size()
                    ? 0 : -1;
            }
            if (reason == LWS_CALLBACK_RECEIVE) return -1;
            if (reason != LWS_CALLBACK_HTTP) return 0;
            char* uri = nullptr; int uri_length = 0;
            const auto method = lws_http_get_uri_and_method(wsi, &uri, &uri_length);
            std::string_view method_name;
            switch (method) {
            case LWSHUMETH_GET: method_name = "GET"; break;
            case LWSHUMETH_POST: method_name = "POST"; break;
            default: method_name = "OTHER"; break;
            }
            std::string_view path;
            if (uri && uri_length >= 0) path = {uri, static_cast<std::size_t>(uri_length)};
            else if (in) path = {static_cast<const char*>(in), length};
            const auto response = route_api_request(method_name, path, self->state_provider);

            std::array<unsigned char, LWS_PRE + 512> headers{};
            auto* start = headers.data() + LWS_PRE;
            auto* cursor = start;
            auto* end = headers.data() + headers.size();
            if (lws_add_http_common_headers(wsi, static_cast<unsigned int>(response.status),
                    response.content_type.c_str(), response.body.size(), &cursor, end) ||
                lws_finalize_write_http_header(wsi, start, &cursor, end)) return -1;
            if (!response.body.empty() &&
                lws_write_http(wsi, response.body.data(), response.body.size()) < 0) return -1;
            return lws_http_transaction_completed(wsi) ? -1 : 0;
        } catch (...) {
            (void)lws_return_http_status(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, nullptr);
            return -1;
        }
    }

    Implementation(std::string listen_address, int listen_port, ApiStateProvider provider)
        : address(std::move(listen_address)), port(listen_port), state_provider(std::move(provider)) {
        if (!state_provider) throw std::invalid_argument("API state provider is empty");
        websocket_state = state_provider();
        if (port < 0 || port > 65535) throw std::invalid_argument("API port is outside 0..65535");
        protocols[0] = {"http", callback, 0, 0, 0, nullptr, 0};
        protocols[1] = LWS_PROTOCOL_LIST_TERM;
        lws_context_creation_info info{};
        info.iface = address.c_str(); info.port = port; info.protocols = protocols.data(); info.user = this;
        info.vhost_name = "picdplayer";
        info.count_threads = 1; info.max_http_header_data = 2048; info.max_http_header_pool = 4;
        info.pt_serv_buf_size = 4096;
        info.options = LWS_SERVER_OPTION_FAIL_UPON_UNABLE_TO_BIND |
                       LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
        lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);
        context = lws_create_context(&info);
        if (!context) throw std::runtime_error("failed to create API server on " + address + ':' + std::to_string(port));
        if (port == 0) {
            auto* vhost = lws_get_vhost_by_name(context, "picdplayer");
            if (!vhost || (port = lws_get_vhost_listen_port(vhost)) <= 0)
                throw std::runtime_error("failed to discover API test port");
        }
    }
    ~Implementation() { if (context) lws_context_destroy(context); }
};

ApiServer::ApiServer(std::string address, int port, ApiStateProvider provider)
    : implementation_(std::make_unique<Implementation>(std::move(address), port, std::move(provider))) {}
ApiServer::~ApiServer() = default;
void ApiServer::publish_state(std::string_view state_json) {
    if (state_json == implementation_->websocket_state) return;
    implementation_->websocket_state = state_json;
    lws_callback_on_writable_all_protocol(implementation_->context,
                                          &implementation_->protocols[0]);
}
void ApiServer::service() {
    if (lws_service(implementation_->context, 0) < 0) throw std::runtime_error("API service failed");
}
int ApiServer::port() const { return implementation_->port; }
