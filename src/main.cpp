#include "cec_device.hpp"
#include "optical_drive.hpp"
#include "cd_device.hpp"
#include <poll.h>
#include <string_view>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <sys/signalfd.h>
#include <unistd.h>

int main(int argc, char** argv) {
    bool cec_enabled = true;
    bool probe_drives = false;
    std::string media_device;
    std::string toc_device;
    std::string device = "/dev/cec0";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--probe-drives") probe_drives = true;
        else if (arg == "--probe-media" && i + 1 < argc) media_device = argv[++i];
        else if (arg == "--probe-toc" && i + 1 < argc) toc_device = argv[++i];
        else if (arg == "--no-cec") cec_enabled = false;
        else if (arg == "--cec-device" && i + 1 < argc) device = argv[++i];
        else {
            std::cerr << "Usage: cdplayerd [--no-cec] [--cec-device PATH] [--probe-drives | --probe-media PATH | --probe-toc PATH]\n";
            return arg == "--help" ? 0 : 2;
        }
    }
    if (int(probe_drives) + int(!media_device.empty()) + int(!toc_device.empty()) > 1) {
        std::cerr << "Choose only one diagnostic mode\n";
        return 2;
    }
    try {
        if (!toc_device.empty()) {
            probe_cd_toc(toc_device);
            return 0;
        }
        if (!media_device.empty()) {
            probe_cd_media(media_device);
            return 0;
        }
        if (probe_drives) {
            const auto drives = find_optical_drives();
            if (drives.empty()) std::cout << "cd: no optical drive detected\n";
            for (const auto& drive : drives)
                std::cout << "cd: device=" << drive.device.string()
                          << " vendor=\"" << drive.vendor << "\" model=\""
                          << drive.model << "\"\n";
            return 0;
        }
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0)
            throw std::system_error(errno, std::generic_category(), "sigprocmask");
        const int fd = signalfd(-1, &mask, SFD_CLOEXEC);
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "signalfd");
        std::cout << "cdplayerd: started\n" << std::flush;
        CecDevice cec(device);
        pollfd signal_poll{fd, POLLIN, 0};
        while (true) {
            if (cec_enabled) cec.update();
            const int result = poll(&signal_poll, 1, cec_enabled ? 250 : -1);
            if (result < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "poll");
            }
            if (signal_poll.revents & POLLIN) break;
            if (signal_poll.revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("signal descriptor failure");
        }
        signalfd_siginfo info{};
        const auto count = read(fd, &info, sizeof(info));
        const int error = errno;
        close(fd);
        if (count != sizeof(info)) throw std::system_error(error, std::generic_category(), "read signal");
        std::cout << "cdplayerd: shutdown signal=" << info.ssi_signo << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cdplayerd: " << error.what() << '\n';
        return 1;
    }
}
