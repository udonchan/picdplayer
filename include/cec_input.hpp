#pragma once

#include <cstdint>
#include <chrono>
#include <optional>
#include <string_view>

// A semantic command received through the CEC Remote Control Passthrough
// feature. It deliberately contains no PlayerController dependency.
enum class CecCommand { play, pause, stop, next, previous, seek_forward, seek_backward };
enum class CecNavigation { up, down, left, right, select, back };

std::optional<CecCommand> cec_command_from_ui_code(std::uint8_t ui_code);
std::string_view cec_command_name(CecCommand command);
std::optional<CecNavigation> cec_navigation_from_ui_code(std::uint8_t ui_code);
std::string_view cec_navigation_name(CecNavigation navigation);

class CecNavigationFilter {
public:
    std::optional<CecNavigation> press(std::uint8_t code, std::uint8_t source,
                                       std::chrono::steady_clock::time_point now);
    void release(std::uint8_t source);
private:
    std::optional<std::uint8_t> pressed_code_;
    std::uint8_t source_ = 0xff;
    std::chrono::steady_clock::time_point last_received_{};
    std::chrono::steady_clock::time_point last_emitted_{};
};
