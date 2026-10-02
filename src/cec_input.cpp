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

std::optional<CecNavigation> cec_navigation_from_ui_code(std::uint8_t ui_code) {
    switch (ui_code) {
    case CEC_OP_UI_CMD_UP: return CecNavigation::up;
    case CEC_OP_UI_CMD_DOWN: return CecNavigation::down;
    case CEC_OP_UI_CMD_LEFT: return CecNavigation::left;
    case CEC_OP_UI_CMD_RIGHT: return CecNavigation::right;
    case CEC_OP_UI_CMD_SELECT: return CecNavigation::select;
    case CEC_OP_UI_CMD_BACK: return CecNavigation::back;
    default: return std::nullopt;
    }
}

std::string_view cec_navigation_name(CecNavigation navigation) {
    switch (navigation) {
    case CecNavigation::up: return "up";
    case CecNavigation::down: return "down";
    case CecNavigation::left: return "left";
    case CecNavigation::right: return "right";
    case CecNavigation::select: return "select";
    case CecNavigation::back: return "back";
    }
    return "unknown";
}

std::optional<CecNavigation> CecNavigationFilter::press(
    std::uint8_t code, std::uint8_t source, std::chrono::steady_clock::time_point now) {
    const auto navigation = cec_navigation_from_ui_code(code);
    if (!navigation) return std::nullopt;
    const bool repeated = pressed_code_ == code && source_ == source &&
        now - last_received_ < std::chrono::milliseconds(500);
    pressed_code_ = code;
    source_ = source;
    last_received_ = now;
    const bool direction = *navigation != CecNavigation::select && *navigation != CecNavigation::back;
    if (repeated && (!direction || now - last_emitted_ < std::chrono::milliseconds(150)))
        return std::nullopt;
    last_emitted_ = now;
    return navigation;
}

void CecNavigationFilter::release(std::uint8_t source) {
    if (source_ == source) pressed_code_.reset();
}
