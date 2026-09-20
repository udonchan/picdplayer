#pragma once
#include <map>
#include <string>
#include <string_view>

struct UiAsset { std::string mime; std::string bytes; };
// Immutable after startup. No filesystem access on HTTP requests.
class UiBundle {
public:
    static UiBundle load(const std::string& directory);
    bool custom() const { return !entry_.empty(); }
    const std::string& error() const { return error_; }
    const UiAsset* find(std::string_view path) const;
private:
    std::map<std::string, UiAsset, std::less<>> assets_;
    std::string entry_, error_;
};
