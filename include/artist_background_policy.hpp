#pragma once

#include <string>
#include <string_view>

// Main-thread owner. Availability is a technical capability, not image readiness.
class ArtistBackgroundPolicy {
public:
    explicit ArtistBackgroundPolicy(bool available = false,
                                    std::string_view reason = "runtime_unavailable")
        : available_(available), reason_(reason) {}
    bool set_enabled(bool enabled) {
        if (enabled && !available()) return false;
        requested_ = enabled;
        return true;
    }
    bool effective() const { return requested_ && available(); }
    bool available() const { return available_; }
    std::string_view unavailable_reason() const { return reason_; }
    std::string json() const {
        return std::string("{\"requested\":") + (requested_ ? "true" : "false") +
            ",\"effective\":" + (effective() ? "true" : "false") +
            ",\"available\":" + (available() ? "true" : "false") +
            ",\"unavailable_reason\":" + (available() ? std::string("null") :
                "\"" + std::string(unavailable_reason()) + "\"") + "}";
    }
private:
    bool requested_ = false;
    bool available_ = false;
    std::string reason_;
};
