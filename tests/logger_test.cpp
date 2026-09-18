#include "logger.hpp"

#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>

int main() {
    std::ostringstream normal;
    std::ostringstream errors;
    {
        AsyncLogger logger(normal, errors, 8);
        logger.submit(LogLevel::info, "player", "state=PLAYING");
        logger.submit(LogLevel::warning, "player", "main_loop_stall stage=test");
    }
    const auto info = normal.str();
    const auto warning = errors.str();
    assert(info.find("Z +") != std::string::npos);
    assert(info.find(" INFO player: state=PLAYING") != std::string::npos);
    assert(warning.find(" WARN player: main_loop_stall stage=test") != std::string::npos);

    bool rejected = false;
    try { AsyncLogger invalid(normal, errors, 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}
