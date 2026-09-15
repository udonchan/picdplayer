#include "cec_device.hpp"
#include "optical_drive.hpp"
#include "cd_device.hpp"
#include "cdda_probe.hpp"
#include "player_session.hpp"
#include <charconv>
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
    bool cec_diagnostics = false;
    bool probe_drives = false;
    std::string cdda_device, backend_name, pcm_output, player_device;
    std::string audio_device = "plughw:CARD=vc4hdmi,DEV=0";
    bool probe_only_options = false, audio_option = false;
    int cdda_track = 1, cdda_frames = 75, cdda_retries = 0;
    bool cdda_options = false;
    std::string media_device;
    std::string toc_device;
    std::string device = "/dev/cec0";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--probe-drives") probe_drives = true;
        else if (arg == "--probe-media" && i + 1 < argc) media_device = argv[++i];
        else if (arg == "--probe-toc" && i + 1 < argc) toc_device = argv[++i];
        else if (arg == "--player" && i + 1 < argc) player_device = argv[++i];
        else if (arg == "--audio-device" && i + 1 < argc) { audio_device = argv[++i]; audio_option = true; }
        else if (arg == "--probe-cdda" && i + 1 < argc) cdda_device = argv[++i];
        else if (arg == "--pcm-output" && i + 1 < argc) {
            probe_only_options = true;
            pcm_output = argv[++i];
            if (pcm_output.empty() || pcm_output == "-") {
                std::cerr << "--pcm-output requires a file path (not stdout)\n";
                return 2;
            }
            cdda_options = true;
        }
        else if (arg == "--cdda-reader" && i + 1 < argc) { backend_name = argv[++i]; cdda_options = true; }
        else if ((arg == "--track" || arg == "--frames" || arg == "--direct-retries") && i + 1 < argc) {
            probe_only_options = true;
            const std::string_view value(argv[++i]);
            int parsed = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            const int min = arg == "--direct-retries" ? 0 : 1;
            const int max = arg == "--direct-retries" ? 10 : (arg == "--track" ? 99 : 750);
            if (error != std::errc{} || end != value.data() + value.size() || parsed < min || parsed > max) {
                std::cerr << "Invalid " << arg << ": expected " << min << ".." << max << '\n';
                return 2;
            }
            if (arg == "--track") cdda_track = parsed;
            else if (arg == "--frames") cdda_frames = parsed;
            else cdda_retries = parsed;
            cdda_options = true;
        }
        else if (arg == "--no-cec") cec_enabled = false;
        else if (arg == "--cec-diagnostics") cec_diagnostics = true;
        else if (arg == "--cec-device" && i + 1 < argc) device = argv[++i];
        else {
            std::cerr << "Usage: cdplayerd [--no-cec] [--cec-device PATH] [--cec-diagnostics] [--probe-drives | --probe-media PATH | --probe-toc PATH | --probe-cdda PATH --cdda-reader direct|paranoia [--track N] [--frames 1..750] [--direct-retries 0..10] [--pcm-output PATH] | --player PATH --cdda-reader direct|paranoia [--audio-device PCM]]\n";
            return arg == "--help" ? 0 : 2;
        }
    }
    if (int(probe_drives) + int(!media_device.empty()) + int(!toc_device.empty()) + int(!cdda_device.empty()) + int(!player_device.empty()) > 1) {
        std::cerr << "Choose only one diagnostic mode\n";
        return 2;
    }
    if (cdda_options && cdda_device.empty() && player_device.empty()) {
        std::cerr << "CDDA options require --probe-cdda\n";
        return 2;
    }
    if (!cdda_device.empty() && backend_name.empty()) {
        std::cerr << "--probe-cdda requires explicit --cdda-reader\n";
        return 2;
    }
    if ((!player_device.empty() && probe_only_options) || (audio_option && player_device.empty())) {
        std::cerr << "Player accepts --cdda-reader, --audio-device and CEC options only\n";
        return 2;
    }
    if (!player_device.empty() && backend_name.empty()) {
        std::cerr << "--player requires explicit --cdda-reader\n"; return 2;
    }
    CddaBackend backend = CddaBackend::direct;
    if (!cdda_device.empty() || !player_device.empty()) {
        try {
            backend = parse_cdda_backend(backend_name);
            require_cdda_backend(backend);
        } catch (const std::invalid_argument& error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    try {
        if (!player_device.empty()) {
            run_player_session(player_device, backend, audio_device, cec_enabled, device, cec_diagnostics);
            return 0;
        }
        if (!cdda_device.empty()) {
            probe_cdda(cdda_device, backend, cdda_track, cdda_frames, cdda_retries, pcm_output);
            return 0;
        }
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
        CecDevice cec(device, cec_diagnostics);
        pollfd polls[]{{fd, POLLIN, 0}, {-1, POLLIN, 0}};
        while (true) {
            if (cec_enabled) cec.update();
            polls[1].fd = cec_enabled ? cec.poll_fd() : -1;
            const int result = poll(polls, 2, cec_enabled ? 250 : -1);
            if (result < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "poll");
            }
            if (polls[0].revents & POLLIN) break;
            if (polls[0].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("signal descriptor failure");
            if (polls[1].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("CEC descriptor failure");
            if (polls[1].revents & POLLIN)
                while (cec.receive().dequeued) {}
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
