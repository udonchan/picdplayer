#include "pcm_file.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <csignal>
#include <sys/resource.h>

void check(bool ok) { if (!ok) throw std::runtime_error("PCM file test failed"); }
int main() {
    char pattern[] = "/tmp/picdplayer-pcm-XXXXXX";
    const auto directory = mkdtemp(pattern);
    if (!directory) return 1;
    const std::filesystem::path root(directory);
    try {
        const std::array<std::int16_t, 5> samples{0, 1, -1, -32768, 0x1234};
        const auto path = (root / "sample.pcm").string();
        save_pcm_s16le(path, samples);
        std::ifstream file(path, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        check(bytes == std::vector<unsigned char>({0,0,1,0,255,255,0,128,0x34,0x12}));
        bool refused = false;
        try { save_pcm_s16le(path, samples); } catch (const std::system_error&) { refused = true; }
        check(refused && std::filesystem::file_size(path) == 10);
        bool missing = false;
        try { save_pcm_s16le((root / "missing/file.pcm").string(), samples); }
        catch (const std::system_error&) { missing = true; }
        check(missing);
        rlimit original{};
        check(getrlimit(RLIMIT_FSIZE, &original) == 0);
        auto limited = original;
        limited.rlim_cur = 4;
        const auto handler = std::signal(SIGXFSZ, SIG_IGN);
        check(setrlimit(RLIMIT_FSIZE, &limited) == 0);
        bool failed = false;
        const auto partial = (root / "partial.pcm").string();
        try { save_pcm_s16le(partial, samples); } catch (const std::system_error&) { failed = true; }
        check(setrlimit(RLIMIT_FSIZE, &original) == 0);
        std::signal(SIGXFSZ, handler);
        check(failed && !std::filesystem::exists(partial));
        std::filesystem::remove_all(root);
        std::cout << "PASS: endian encoding, no overwrite, create/write failures\n";
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root);
        std::cerr << error.what() << '\n'; return 1;
    }
}
