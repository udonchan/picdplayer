#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// A semantic command received through the CEC Remote Control Passthrough
// feature. It deliberately contains no PlayerController dependency.
enum class CecCommand { play, pause, stop, next, previous, seek_forward, seek_backward };

std::optional<CecCommand> cec_command_from_ui_code(std::uint8_t ui_code);
std::string_view cec_command_name(CecCommand command);
