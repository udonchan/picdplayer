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
        std::cout << "PASS: CEC UI command mapping\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
