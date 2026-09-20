#include "ui_bundle.hpp"
#include "api_server.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
namespace fs = std::filesystem;
void check(bool b) { if (!b) throw std::runtime_error("UI test failed"); }
void put(const fs::path& p, const std::string& s) { std::ofstream(p) << s; }
int main() {
    char tmp[] = "/tmp/picdplayer-ui-XXXXXX";
    if (!mkdtemp(tmp)) return 1;
    const fs::path root(tmp);
    const std::string manifest = R"({"picdplayer_ui":1,"requires_api":1,"name":"test","entry":"index.html"})";
    try {
        check(!UiBundle::load("").custom() && UiBundle::load("").error().empty());
        auto fallback = [&](const fs::path& path) {
            auto ui = UiBundle::load(path.string());
            check(!ui.custom() && !ui.error().empty());
            auto r = route_api_request("GET", "/player", [] { return "{}"; }, {}, {}, {}, &ui);
            check(r.status == 200 && r.body.find("CUSTOM UI DISABLED") != r.body.npos);
        };
        fallback(root / "absent");
        fallback(root);
        put(root / "manifest.json", "{"); fallback(root);
        put(root / "manifest.json", manifest); fallback(root);
        put(root / "index.html", "custom content"); put(root / "player.css", "body{}"); put(root / "player.js", "// js");
        auto ui = UiBundle::load(root.string()); check(ui.custom());
        check(ui.find("/player")->bytes == "custom content");
        check(!ui.find("/player/../manifest.json"));
        check(!ui.find("/player/%2e%2e/index.html"));
        auto r = route_api_request("GET", "/player", [] { return "{}"; }, {}, {}, {}, &ui);
        check(r.status == 200 && r.body == "custom content");
        check(route_api_request("GET", "/builtin/player", [] { return "{}"; }, {}, {}, {}, &ui).body.find("/builtin/player.js") != std::string::npos);
        check(route_api_request("POST", "/player", [] { return "{}"; }, {}, {}, {}, &ui).status == 405);
        put(root / "index.html", "edited"); check(ui.find("/player")->bytes == "custom content");
        for (auto bad : {R"({"picdplayer_ui":2,"requires_api":1,"name":"test","entry":"index.html"})",
                         R"({"picdplayer_ui":1,"requires_api":2,"name":"test","entry":"index.html"})",
                         R"({"picdplayer_ui":1,"requires_api":1,"name":"test","entry":"../index.html"})",
                         R"({"picdplayer_ui":1,"requires_api":1,"name":"test","entry":"/index.html"})",
                         R"({"picdplayer_ui":1,"requires_api":1,"entry":"index.html"})"}) {
            put(root / "manifest.json", bad); fallback(root);
        }
        put(root / "manifest.json", std::string(100, '[') + std::string(100, ']')); fallback(root);
        put(root / "manifest.json", manifest);
        check(mkfifo((root / "pipe").c_str(), 0600) == 0); fallback(root); fs::remove(root / "pipe");
        put(root / "bad.js", std::string(1, static_cast<char>(0xff))); fallback(root); fs::remove(root / "bad.js");
        fs::create_symlink("index.html", root / "link.html"); fallback(root); fs::remove(root / "link.html");
        fs::create_directory_symlink(root, root / "linked"); fallback(root / "linked"); fs::remove(root / "linked");
        put(root / "huge.js", std::string(256 * 1024 + 1, 'x')); fallback(root); fs::remove(root / "huge.js");
        auto nested = root;
        for (int i=0;i<9;++i) { nested /= "deep"; fs::create_directory(nested); }
        fallback(root); fs::remove_all(root / "deep");
        for (int i=0;i<65;++i) put(root / (std::to_string(i)+".css"), "");
        fallback(root);
        for (int i=0;i<65;++i) fs::remove(root / (std::to_string(i)+".css"));
        for (int i=0;i<9;++i) put(root / (std::to_string(i)+".js"), std::string(256*1024,'x'));
        fallback(root);
        fs::remove_all(root);
        std::cout << "UI bundle and fallback routes passed\n";
    } catch (const std::exception& e) { fs::remove_all(root); std::cerr << e.what() << '\n'; return 1; }
}
