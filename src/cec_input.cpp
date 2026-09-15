#include "cec_input.hpp"
#include <linux/cec.h>

std::optional<CecCommand> cec_command_from_ui_code(std::uint8_t ui_code) {
    switch (ui_code) {
    case CEC_OP_UI_CMD_PLAY: return CecCommand::play;
    case CEC_OP_UI_CMD_PAUSE: return CecCommand::pause;
    case CEC_OP_UI_CMD_STOP: return CecCommand::stop;
    case CEC_OP_UI_CMD_SKIP_FORWARD: return CecCommand::next;
    case CEC_OP_UI_CMD_SKIP_BACKWARD: return CecCommand::previous;
    case CEC_OP_UI_CMD_FAST_FORWARD: return CecCommand::seek_forward;
    case CEC_OP_UI_CMD_REWIND: return CecCommand::seek_backward;
    default: return std::nullopt;
    }
}

std::string_view cec_command_name(CecCommand command) {
    switch (command) {
    case CecCommand::play: return "play";
    case CecCommand::pause: return "pause";
    case CecCommand::stop: return "stop";
    case CecCommand::next: return "next";
    case CecCommand::previous: return "previous";
    case CecCommand::seek_forward: return "seek_forward";
    case CecCommand::seek_backward: return "seek_backward";
    }
    return "unknown";
}
