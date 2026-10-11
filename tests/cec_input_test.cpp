#include "cec_input.hpp"
#include <iostream>
#include <stdexcept>

void check(bool value) { if (!value) throw std::runtime_error("CEC input test failed"); }

int main() {
    try {
        check(cec_command_from_ui_code(0x44) == CecCommand::play);
        check(cec_command_from_ui_code(0x46) == CecCommand::pause);
        check(cec_command_from_ui_code(0x45) == CecCommand::stop);
        check(cec_command_from_ui_code(0x4b) == CecCommand::next);
        check(cec_command_from_ui_code(0x4c) == CecCommand::previous);
        check(cec_command_from_ui_code(0x49) == CecCommand::seek_forward);
        check(cec_command_from_ui_code(0x48) == CecCommand::seek_backward);
        check(!cec_command_from_ui_code(0x41));
        check(cec_command_name(CecCommand::previous) == "previous");
        check(cec_command_name(CecCommand::seek_backward) == "seek_backward");
        check(cec_navigation_from_ui_code(0x01) == CecNavigation::up);
        check(cec_navigation_from_ui_code(0x02) == CecNavigation::down);
        check(cec_navigation_from_ui_code(0x03) == CecNavigation::left);
        check(cec_navigation_from_ui_code(0x04) == CecNavigation::right);
        check(cec_navigation_from_ui_code(0x00) == CecNavigation::select);
        check(cec_navigation_from_ui_code(0x0d) == CecNavigation::back);
        check(!cec_navigation_from_ui_code(0x44));
        check(cec_navigation_name(CecNavigation::select) == "select");
        CecNavigationFilter filter;
        const auto at = std::chrono::steady_clock::time_point{};
        check(filter.press(0x00, 1, at) == CecNavigation::select);
        check(!filter.press(0x00, 1, at + std::chrono::milliseconds(200)));
        check(!filter.press(0x00, 1, at + std::chrono::milliseconds(400)));
        filter.release(2); // another source cannot release this key
        check(!filter.press(0x00, 1, at + std::chrono::milliseconds(450)));
        filter.release(1);
        check(filter.press(0x00, 1, at + std::chrono::milliseconds(460)) == CecNavigation::select);
        check(filter.press(0x01, 1, at + std::chrono::milliseconds(470)) == CecNavigation::up);
        check(!filter.press(0x01, 1, at + std::chrono::milliseconds(500)));
        check(filter.press(0x01, 1, at + std::chrono::milliseconds(630)) == CecNavigation::up);
        check(filter.press(0x00, 1, at + std::chrono::milliseconds(1200)) == CecNavigation::select);
        std::cout << "PASS: CEC UI command mapping\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
