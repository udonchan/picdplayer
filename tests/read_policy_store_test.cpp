#include "read_policy_store.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {
void check(bool result) { if (!result) throw std::runtime_error("read policy store test failed"); }
}

int main() {
    const auto directory = fs::temp_directory_path() /
        ("picdplayer-settings-test-" + std::to_string(getpid()));
    fs::create_directories(directory);
    try {
        const auto path = (directory / "settings.json").string();
        std::string error;
        check(!load_saved_read_policy(path, 300, error) && error.empty());
        const ReadPolicy policy{ReadVerificationMode::repeat, 75, 2, 3, 5000};
        check(save_read_policy(path, policy, 300, error) && error.empty());
        const auto loaded = load_saved_read_policy(path, 300, error);
        check(loaded && loaded->mode == ReadVerificationMode::repeat &&
              loaded->region_frames == 75 && loaded->time_budget_ms == 5000);
        check(!save_read_policy(path, policy, 30, error) && !error.empty());
        check(load_saved_read_policy(path, 300, error)->time_budget_ms == 5000);

        { std::ofstream output(path, std::ios::trunc); output << "{broken"; }
        check(!load_saved_read_policy(path, 300, error) && !error.empty());
        { std::ofstream output(path, std::ios::trunc); output <<
            R"({"schema_version":2,"read_policy":{}})"; }
        check(!load_saved_read_policy(path, 300, error) && !error.empty());
        fs::remove(path);
        fs::create_symlink(directory / "missing", path);
        check(!load_saved_read_policy(path, 300, error) && !error.empty());
        fs::remove(path);
        check(!save_read_policy((directory / "missing" / "settings.json").string(),
                                policy, 300, error) && !error.empty());
    } catch (...) {
        fs::remove_all(directory);
        throw;
    }
    fs::remove_all(directory);
}
