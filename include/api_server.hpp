#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct ApiResponse { int status; std::string content_type; std::string body; };
enum class ApiCommandType { play, pause, stop, next, previous, seek_relative, select_track, eject };
struct ApiCommand { ApiCommandType type; int value = 0; };
using ApiStateProvider = std::function<std::string()>;
using ApiCommandHandler = std::function<bool(const ApiCommand&)>;

ApiResponse route_api_request(std::string_view method, std::string_view path,
                              const ApiStateProvider& state_provider,
                              const ApiCommandHandler& command_handler = {},
                              std::string_view body = {});

class ApiServer {
public:
    ApiServer(std::string listen_address, int port, ApiStateProvider state_provider,
              ApiCommandHandler command_handler = {});
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
