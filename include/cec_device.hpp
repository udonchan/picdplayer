#pragma once
#include "cec_input.hpp"
#include <cstdint>
#include <optional>
#include <string>

struct CecReceiveResult {
    bool dequeued = false;
    std::optional<CecCommand> command;
};

class CecDevice {
public:
    explicit CecDevice(std::string path, bool diagnostics = false);
    ~CecDevice();
    CecDevice(const CecDevice&) = delete;
    CecDevice& operator=(const CecDevice&) = delete;
    void update();
    int poll_fd() const noexcept;
    // Dequeues one CEC message. Releases and unrelated traffic have no command.
    CecReceiveResult receive();
private:
    std::string path_;
    std::string last_status_;
    int fd_ = -1;
    bool requested_ = false;
    bool diagnostics_ = false;
    std::uint16_t physical_ = 0xffff;
    std::uint8_t logical_ = 0xff;
    bool active_source_ = false;
    void status(const std::string& message);
    void report_active_source(const char* reason);
    void set_active_source(bool active, const char* reason, std::uint16_t path);
};
