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
    expect(::unsetenv("FANART_TV_API_KEY") == 0);
    expect(resolve_artist_background_key("").empty());
    expect(::setenv("FANART_TV_API_KEY", "", 1) == 0);
    expect(resolve_artist_background_key("").empty());
    expect(::setenv("FANART_TV_API_KEY", "fixture-environment-key", 1) == 0);
    expect(resolve_artist_background_key("") == "fixture-environment-key");
    for (const auto& value : {std::string("bad key"), std::string("bad\nkey"),
                             std::string(257, 'x'), std::string("bad\x80")}) {
        expect(::setenv("FANART_TV_API_KEY", value.c_str(), 1) == 0);
        bool invalid = false;
        try { (void)resolve_artist_background_key(""); }
        catch (const std::exception& error) {
            invalid = true;
            expect(std::string(error.what()).find(value) == std::string::npos);
        }
        expect(invalid);
    }
    expect(::setenv("FANART_TV_API_KEY", "fixture-environment-key", 1) == 0);
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
    expect(resolve_artist_background_key(path) == "test-api-key");
    expect(::symlink(path.c_str(), link.c_str()) == 0);
    expect(rejected(link));
    ::unlink(link.c_str());
    expect(::chmod(path.c_str(), 0666) == 0);
    expect(rejected(path));
    expect(::chmod(path.c_str(), 0600) == 0);
    expect(::chmod(path.c_str(), 0644) == 0);
    expect(rejected(path));
    expect(::chmod(path.c_str(), 0640) == 0);
    expect(load_artist_background_key(path) == "test-api-key");
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
    bool invalid_file = false;
    try { (void)resolve_artist_background_key(path); }
    catch (const std::exception&) { invalid_file = true; }
    expect(invalid_file);
    expect(::unsetenv("FANART_TV_API_KEY") == 0);
}
