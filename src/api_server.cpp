#include "api_server.hpp"
#include "now_playing_page.hpp"
#include "technical_status_page.hpp"
#include "pcm_worker.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <libwebsockets.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

ApiResponse route_api_request(std::string_view method, std::string_view path,
                              const ApiStateProvider& state_provider,
                              const ApiCommandHandler& command_handler,
                              std::string_view body,
                              const ApiReadPolicyProvider& read_policy_provider, const UiBundle* ui) {
    if (path == "/builtin/player" || path == "/builtin/player.css" || path == "/builtin/player.js") {
        if (method != "GET") return {405, "application/json", R"({"error":"method_not_allowed"})"};
        if (path == "/builtin/player.css") return {200, "text/css; charset=utf-8", std::string(now_playing_css())};
        if (path == "/builtin/player.js") return {200, "text/javascript; charset=utf-8", std::string(now_playing_javascript())};
        auto html = std::string(now_playing_html());
        for (const auto* asset : {"/player.css", "/player.js"}) {
            auto pos = html.find(asset);
            if (pos != html.npos) html.insert(pos, "/builtin");
        }
        return {200, "text/html; charset=utf-8", std::move(html)};
    }
    if (ui && ui->custom() && (path == "/player" || path == "/player.css" || path == "/player.js" || path.starts_with("/player/"))) {
        if (method != "GET") return {405, "application/json", R"({"error":"method_not_allowed"})"};
        if (const auto* asset = ui->find(path)) return {200, asset->mime, asset->bytes};
        return {404, "application/json", R"({"error":"not_found"})"};
    }
    if (path == "/player/") path = "/player";
    if (path == "/player" || path == "/player.css" || path == "/player.js") {
        if (method != "GET")
            return {HTTP_STATUS_METHOD_NOT_ALLOWED, "application/json", R"({"error":"method_not_allowed"})"};
        if (path == "/player") {
            auto html = std::string(now_playing_html());
            if (ui && !ui->error().empty()) {
                const auto at = html.find("<main>");
                if (at != html.npos) html.insert(at + 6,
                    "<aside role=\"alert\">CUSTOM UI DISABLED — Custom UI validation failed. Using built-in UI.</aside>");
            }
            return {HTTP_STATUS_OK, "text/html; charset=utf-8", std::move(html)};
        }
        if (path == "/player.css")
            return {HTTP_STATUS_OK, "text/css; charset=utf-8", std::string(now_playing_css())};
        return {HTTP_STATUS_OK, "text/javascript; charset=utf-8", std::string(now_playing_javascript())};
    }
    if (path == "/debug/status" || path == "/debug/status.css" || path == "/debug/status.js") {
        if (method != "GET")
            return {HTTP_STATUS_METHOD_NOT_ALLOWED, "application/json", R"({"error":"method_not_allowed"})"};
        if (path == "/debug/status")
            return {HTTP_STATUS_OK, "text/html; charset=utf-8", std::string(technical_status_html())};
        if (path == "/debug/status.css")
            return {HTTP_STATUS_OK, "text/css; charset=utf-8", std::string(technical_status_css())};
        return {HTTP_STATUS_OK, "text/javascript; charset=utf-8",
                std::string(technical_status_javascript())};
    }
    if (path == "/api/state") {
        if (method != "GET")
            return {HTTP_STATUS_METHOD_NOT_ALLOWED, "application/json", R"({"error":"method_not_allowed"})"};
        auto body = state_provider();
        constexpr std::size_t maximum_state_bytes = 1024 * 1024;
        if (body.size() > maximum_state_bytes)
            return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "application/json", R"({"error":"state_too_large"})"};
        return {HTTP_STATUS_OK, "application/json", std::move(body)};
    }
    if (path == "/api/read-policy" && method == "GET") {
        if (!read_policy_provider)
            return {HTTP_STATUS_SERVICE_UNAVAILABLE, "application/json", R"({"error":"read_policy_unavailable"})"};
        auto body = read_policy_provider();
        if (body.size() > 4096)
            return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "application/json", R"({"error":"policy_too_large"})"};
        return {HTTP_STATUS_OK, "application/json", std::move(body)};
    }
    auto command = [&]() -> std::optional<ApiCommand> {
        if (path == "/api/play") return ApiCommand{ApiCommandType::play};
        if (path == "/api/pause") return ApiCommand{ApiCommandType::pause};
        if (path == "/api/stop") return ApiCommand{ApiCommandType::stop};
        if (path == "/api/next") return ApiCommand{ApiCommandType::next};
        if (path == "/api/previous") return ApiCommand{ApiCommandType::previous};
        if (path == "/api/seek") return ApiCommand{ApiCommandType::seek_relative};
        if (path == "/api/track") return ApiCommand{ApiCommandType::select_track};
        if (path == "/api/eject") return ApiCommand{ApiCommandType::eject};
        if (path == "/api/read-policy") return ApiCommand{ApiCommandType::set_read_policy};
        return std::nullopt;
    }();
    if (!command)
        return {HTTP_STATUS_NOT_FOUND, "application/json", R"({"error":"not_found"})"};
    if (method != "POST")
        return {HTTP_STATUS_METHOD_NOT_ALLOWED, "application/json", R"({"error":"method_not_allowed"})"};
    if (!command_handler)
        return {HTTP_STATUS_FORBIDDEN, "application/json", R"({"error":"commands_disabled"})"};
    if (command->type == ApiCommandType::set_read_policy ||
        command->type == ApiCommandType::seek_relative || command->type == ApiCommandType::select_track) {
        constexpr std::size_t maximum_command_bytes = 4096;
        if (body.size() > maximum_command_bytes)
            return {HTTP_STATUS_REQ_ENTITY_TOO_LARGE, "application/json", R"({"error":"body_too_large"})"};
        try {
            const auto json = nlohmann::json::parse(body);
            if (command->type == ApiCommandType::set_read_policy) {
                if (!json.is_object() || json.size() != 5 || !json.contains("mode") ||
                    !json["mode"].is_string() || !json.contains("region_frames") ||
                    !json.contains("required_matches") || !json.contains("maximum_attempts") ||
                    !json.contains("time_budget_ms"))
                    return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
                const auto number = [&](const char* key) -> std::optional<unsigned> {
                    if (!json[key].is_number_unsigned()) return std::nullopt;
                    const auto value = json[key].get<unsigned long long>();
                    if (value > std::numeric_limits<unsigned>::max()) return std::nullopt;
                    return static_cast<unsigned>(value);
                };
                const auto region = number("region_frames");
                const auto matches = number("required_matches");
                const auto attempts = number("maximum_attempts");
                const auto budget = number("time_budget_ms");
                if (!region || !matches || !attempts || !budget)
                    return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
                try {
                    command->read_policy = {parse_read_verification_mode(json["mode"].get<std::string>()),
                                            *region, *matches, *attempts, *budget};
                    validate_read_policy(command->read_policy, maximum_buffer_cd_frames);
                } catch (const std::exception&) {
                    return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
                }
            } else {
            const char* field = command->type == ApiCommandType::seek_relative ? "offset_seconds" : "track";
            if (!json.is_object() || json.size() != 1 || !json.contains(field) || !json[field].is_number_integer())
                return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
            if (json[field].is_number_unsigned() &&
                json[field].get<unsigned long long>() > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
                return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
            const auto value = json[field].get<long long>();
            const auto minimum = command->type == ApiCommandType::seek_relative ? -86'400LL : 1LL;
            const auto maximum = command->type == ApiCommandType::seek_relative ? 86'400LL : 99LL;
            if (value < minimum || value > maximum)
                return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
            command->value = static_cast<int>(value);
            }
        } catch (const nlohmann::json::exception&) {
            return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"invalid_body"})"};
        }
    } else if (!body.empty()) {
        return {HTTP_STATUS_BAD_REQUEST, "application/json", R"({"error":"unexpected_body"})"};
    }
    if (!command_handler(*command))
        return {HTTP_STATUS_CONFLICT, "application/json", R"({"error":"command_rejected"})"};
    if (command->type == ApiCommandType::eject)
        return {202, "application/json", {}};
    return {HTTP_STATUS_NO_CONTENT, "application/json", {}};
}

struct ApiServer::Implementation {
    UiBundle ui;
    std::string address;
    int port;
    ApiStateProvider state_provider;
    ApiCommandHandler command_handler;
    ApiReadPolicyProvider read_policy_provider;
    std::string websocket_state;
    std::array<lws_protocols, 2> protocols{};
    lws_context* context = nullptr;
    struct PendingRequest { std::string method; std::string path; std::string body; };
    std::unordered_map<lws*, PendingRequest> pending_requests;

    static bool peer_is_loopback(lws* wsi) {
        std::array<char, 64> address{};
        if (!lws_get_peer_simple(wsi, address.data(), address.size())) return false;
        const std::string_view peer(address.data());
        return peer == "::1" || peer.starts_with("127.") || peer.starts_with("::ffff:127.");
    }

    const ApiCommandHandler& handler_for(lws* wsi) const {
        static const ApiCommandHandler disabled;
        return peer_is_loopback(wsi) ? command_handler : disabled;
    }

    static int send_response(lws* wsi, const ApiResponse& response) {
        std::array<unsigned char, LWS_PRE + 1024> headers{};
        auto* start = headers.data() + LWS_PRE;
        auto* cursor = start;
        auto* end = headers.data() + headers.size();
        const auto add_header = [&](std::string_view name, std::string_view value) {
            return lws_add_http_header_by_name(wsi,
                reinterpret_cast<const unsigned char*>(name.data()),
                reinterpret_cast<const unsigned char*>(value.data()),
                static_cast<int>(value.size()), &cursor, end);
        };
        if (lws_add_http_common_headers(wsi, static_cast<unsigned int>(response.status),
                response.content_type.c_str(), response.body.size(), &cursor, end) ||
            add_header("content-security-policy:", "default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data: https:; style-src 'self'; script-src 'self'; base-uri 'none'; frame-ancestors 'none'; object-src 'none") ||
            add_header("cache-control:", "no-store") ||
            add_header("x-content-type-options:", "nosniff") ||
            add_header("referrer-policy:", "no-referrer") ||
            add_header("x-frame-options:", "DENY") ||
            lws_finalize_write_http_header(wsi, start, &cursor, end)) return -1;
        if (!response.body.empty() &&
            lws_write_http(wsi, response.body.data(), response.body.size()) < 0) return -1;
        return lws_http_transaction_completed(wsi) ? -1 : 0;
    }

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
            if (reason == LWS_CALLBACK_HTTP_BODY) {
                auto found = self->pending_requests.find(wsi);
                if (found == self->pending_requests.end()) return -1;
                constexpr std::size_t maximum_command_bytes = 4096;
                if (length > maximum_command_bytes - std::min(found->second.body.size(), maximum_command_bytes))
                    found->second.body.resize(maximum_command_bytes + 1);
                else
                    found->second.body.append(static_cast<const char*>(in), length);
                return 0;
            }
            if (reason == LWS_CALLBACK_HTTP_BODY_COMPLETION) {
                auto found = self->pending_requests.find(wsi);
                if (found == self->pending_requests.end()) return -1;
                auto request = std::move(found->second);
                self->pending_requests.erase(found);
                return send_response(wsi, route_api_request(request.method, request.path,
                                     self->state_provider, self->handler_for(wsi), request.body,
                                     self->read_policy_provider, &self->ui));
            }
            if (reason == LWS_CALLBACK_CLOSED_HTTP) {
                self->pending_requests.erase(wsi);
                return 0;
            }
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
            std::array<char, 32> content_length{};
            const auto content_length_size = lws_hdr_copy(wsi, content_length.data(),
                                                           content_length.size(),
                                                           WSI_TOKEN_HTTP_CONTENT_LENGTH);
            const auto content_length_value = content_length_size > 0
                ? std::string_view(content_length.data(), static_cast<std::size_t>(content_length_size))
                : std::string_view{};
            if (method_name == "POST" && !content_length_value.empty() && content_length_value != "0") {
                self->pending_requests[wsi] = {std::string(method_name), std::string(path), {}};
                return 0;
            }
            return send_response(wsi, route_api_request(method_name, path, self->state_provider,
                                 self->handler_for(wsi), {}, self->read_policy_provider, &self->ui));
        } catch (...) {
            (void)lws_return_http_status(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, nullptr);
            return -1;
        }
    }

    Implementation(std::string listen_address, int listen_port, ApiStateProvider provider,
                   ApiCommandHandler handler, ApiReadPolicyProvider policy_provider, UiBundle bundle)
        : ui(std::move(bundle)), address(std::move(listen_address)), port(listen_port), state_provider(std::move(provider)),
          command_handler(std::move(handler)), read_policy_provider(std::move(policy_provider)) {
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
        info.options = LWS_SERVER_OPTION_FAIL_UPON_UNABLE_TO_BIND;
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

ApiServer::ApiServer(std::string address, int port, ApiStateProvider provider,
                     ApiCommandHandler handler, ApiReadPolicyProvider policy_provider, UiBundle ui)
    : implementation_(std::make_unique<Implementation>(std::move(address), port, std::move(provider),
                                                        std::move(handler), std::move(policy_provider), std::move(ui))) {}
ApiServer::~ApiServer() = default;
void ApiServer::publish_state(std::string_view state_json) {
    if (state_json == implementation_->websocket_state) return;
    implementation_->websocket_state = state_json;
    lws_callback_on_writable_all_protocol(implementation_->context,
                                          &implementation_->protocols[0]);
}
void ApiServer::service() {
    // Since lws 3.2 the timeout argument is ignored. Queue a wakeup so
    // an idle HTTP server cannot sleep inside the player event loop.
    lws_cancel_service(implementation_->context);
    if (lws_service(implementation_->context, 0) < 0) throw std::runtime_error("API service failed");
}
int ApiServer::port() const { return implementation_->port; }
