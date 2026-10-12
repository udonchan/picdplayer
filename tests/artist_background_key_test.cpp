#include "artist_background_key.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition) {
    if (!condition) throw std::runtime_error("artist background key test failed");
}
bool rejected(const std::string& path) {
    try { (void)load_artist_background_key(path); }
    catch (const std::exception& error) {
        expect(std::string(error.what()).find(path) == std::string::npos);
        return true;
    }
    return false;
}
}

int main() {
    char name[] = "/tmp/picdplayer-artist-key-XXXXXX";
    const int fd = ::mkstemp(name);
    expect(fd >= 0);
    ::close(fd);
    const std::string path(name);
    const std::string link = path + ".link";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "test-api-key\n";
    }
    expect(load_artist_background_key(path) == "test-api-key");
    expect(::symlink(path.c_str(), link.c_str()) == 0);
    expect(rejected(link));
    ::unlink(link.c_str());
    expect(::chmod(path.c_str(), 0666) == 0);
    expect(rejected(path));
    expect(::chmod(path.c_str(), 0600) == 0);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << std::string(258, 'x');
    }
    expect(rejected(path));
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "bad key\n";
    }
    expect(rejected(path));
    ::unlink(path.c_str());
    expect(rejected(path));
}
