#pragma once

#include <string>
#include <string_view>

// Main-thread owner. Runtime delivery is not ready for ON.
class ArtistBackgroundPolicy {
public:
    bool set_enabled(bool enabled) {
        if (enabled) return false;
        requested_ = false;
        return true;
    }
    bool effective() const { return requested_ && available(); }
    bool available() const { return false; }
    std::string_view unavailable_reason() const { return "runtime_unavailable"; }
    std::string json() const {
        return std::string("{\"requested\":") + (requested_ ? "true" : "false") +
            ",\"effective\":" + (effective() ? "true" : "false") +
            ",\"available\":" + (available() ? "true" : "false") +
            ",\"unavailable_reason\":\"" + std::string(unavailable_reason()) + "\"}";
    }
private:
    bool requested_ = false;
};
