#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct ApiResponse { int status; std::string content_type; std::string body; };
using ApiStateProvider = std::function<std::string()>;

ApiResponse route_api_request(std::string_view method, std::string_view path,
                              const ApiStateProvider& state_provider);

class ApiServer {
public:
    ApiServer(std::string listen_address, int port, ApiStateProvider state_provider);
    ~ApiServer();
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;
    void publish_state(std::string_view state_json);
    void service();
    int port() const;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
