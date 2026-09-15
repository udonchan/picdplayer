#pragma once
#include <string>

class CecDevice {
public:
    explicit CecDevice(std::string path);
    ~CecDevice();
    CecDevice(const CecDevice&) = delete;
    CecDevice& operator=(const CecDevice&) = delete;
    void update();
private:
    std::string path_;
    std::string last_status_;
    int fd_ = -1;
    bool requested_ = false;
    void status(const std::string& message);
};
