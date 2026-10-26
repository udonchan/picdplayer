#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "read_policy.hpp"
#include "ui_bundle.hpp"
#include "cec_input.hpp"

struct ApiResponse { int status; std::string content_type; std::string body; };
enum class ApiCommandType { play, pause, stop, next, previous, seek_relative, select_track, eject,
                            set_read_policy, select_metadata_candidate, decline_metadata_candidates,
                            set_artist_background, start_complete_pcm, cancel_complete_pcm };
struct ApiCommand {
    ApiCommandType type;
    int value = 0;
    ReadPolicy read_policy{};
    std::string session_id;
    std::uint64_t disc_generation = 0;
    std::uint64_t metadata_generation = 0;
    std::size_t candidate_index = 0;
    bool enabled = false;
    std::uint64_t job_id = 0;
    unsigned duration_ms = 0;
};
enum class ApiCommandFailure { command_rejected, capture_releasing, capture_not_ready,
                               capture_context_changed, capture_no_job, capture_busy };
struct ApiCommandResult {
    bool accepted;
    ApiCommandFailure failure = ApiCommandFailure::command_rejected;
    ApiCommandResult(bool value) : accepted(value) {}
    ApiCommandResult(ApiCommandFailure value) : accepted(false), failure(value) {}
    explicit operator bool() const { return accepted; }
};
using ApiUiBootHandler = std::function<bool(std::string_view, std::string_view, double)>;
using ApiStateProvider = std::function<std::string()>;
using ApiReadPolicyProvider = std::function<std::string()>;
using ApiCommandHandler = std::function<ApiCommandResult(const ApiCommand&)>;
using ApiCompletePcmProvider = std::function<ApiResponse(bool result)>;
using ApiArtworkProvider = std::function<std::optional<ApiResponse>()>;
using ApiBackgroundArtworkProvider = std::function<std::optional<ApiResponse>(std::string_view)>;

ApiResponse route_api_request(std::string_view method, std::string_view path,
                              const ApiStateProvider& state_provider,
                              const ApiCommandHandler& command_handler = {},
                              std::string_view body = {},
                              const ApiReadPolicyProvider& read_policy_provider = {},
                              const UiBundle* ui = nullptr,
                              const ApiUiBootHandler& ui_boot_handler = {},
                              const ApiArtworkProvider& artwork_provider = {},
                              const ApiStateProvider& read_history_provider = {},
                              const ApiStateProvider& artist_background_provider = {},
                              const ApiBackgroundArtworkProvider& background_artwork_provider = {},
                              const ApiStateProvider& disc_map_provider = {},
                              const ApiCompletePcmProvider& complete_pcm_provider = {});

class ApiServer {
public:
    ApiServer(std::string listen_address, int port, ApiStateProvider state_provider,
              ApiCommandHandler command_handler = {}, ApiReadPolicyProvider read_policy_provider = {},
              UiBundle ui = {}, ApiArtworkProvider artwork_provider = {},
              ApiStateProvider read_history_provider = {}, ApiStateProvider artist_background_provider = {},
              ApiBackgroundArtworkProvider background_artwork_provider = {},
              ApiStateProvider disc_map_provider = {}, ApiCompletePcmProvider complete_pcm_provider = {});
    ~ApiServer();
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;
    void publish_state(std::string_view state_json);
    void publish_navigation(CecNavigation navigation);
    void service();
    int port() const;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
