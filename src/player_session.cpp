#include "player_session.hpp"
#include "playback_engine.hpp"
#include "cd_device.hpp"
#include "cec_device.hpp"
#include "media_state.hpp"
#include "media_worker.hpp"
#ifdef ENABLE_METADATA
#include "metadata_lookup.hpp"
#include "metadata_worker.hpp"
#include "metadata_session.hpp"
#endif
#ifdef ENABLE_API
#include "api_server.hpp"
#include "daemon_snapshot.hpp"
#include "daemon_snapshot_json.hpp"
#endif
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <sys/signalfd.h>
#include <system_error>
#include <unistd.h>

namespace {
struct Signals {
    sigset_t previous{};
    int fd = -1;
    Signals() {
        sigset_t mask;
        sigemptyset(&mask); sigaddset(&mask, SIGINT); sigaddset(&mask, SIGTERM);
        if (sigprocmask(SIG_BLOCK, &mask, &previous) < 0)
            throw std::system_error(errno, std::generic_category(), "block signals");
        fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
        if (fd < 0) {
            const auto error = errno;
            sigprocmask(SIG_SETMASK, &previous, nullptr);
            throw std::system_error(error, std::generic_category(), "signalfd");
        }
    }
    ~Signals() { close(fd); sigprocmask(SIG_SETMASK, &previous, nullptr); }
};
void print_state(const PlayerController& controller) {
    const auto state = controller.state();
    const char* name = "NO_DISC";
    switch (state.playback) {
    case PlaybackState::playing: name = "PLAYING"; break;
    case PlaybackState::paused: name = "PAUSED"; break;
    case PlaybackState::stopped: name = "STOPPED"; break;
    case PlaybackState::no_disc: break;
    }
    std::cout << "player: state=" << name << " track=" << state.track.value_or(0)
              << " lba=" << state.position_lba.value_or(0) << '\n' << std::flush;
}

const char* media_state_name(MediaLifecycleState state) {
    switch (state) {
    case MediaLifecycleState::no_disc: return "NO_DISC";
    case MediaLifecycleState::loading: return "LOADING";
    case MediaLifecycleState::audio_ready: return "AUDIO_READY";
    case MediaLifecycleState::unsupported: return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

bool same_toc(const DiscToc& left, const DiscToc& right) {
    if (left.leadout_lba != right.leadout_lba || left.tracks.size() != right.tracks.size())
        return false;
    for (std::size_t i = 0; i < left.tracks.size(); ++i) {
        const auto& a = left.tracks[i];
        const auto& b = right.tracks[i];
        if (a.number != b.number || a.start_lba != b.start_lba || a.length_frames != b.length_frames)
            return false;
    }
    return true;
}

bool apply_cec_command(PlayerController& controller, CecCommand command) {
    constexpr auto cec_seek_frames = 10 * cd_frames_per_second;
    const auto before = controller.state();
    switch (command) {
    case CecCommand::play: controller.play(); break;
    case CecCommand::pause: controller.pause(); break;
    case CecCommand::stop: controller.stop(); break;
    case CecCommand::next: controller.next(); break;
    case CecCommand::previous: controller.previous(); break;
    case CecCommand::seek_forward: controller.seek_relative(cec_seek_frames); break;
    case CecCommand::seek_backward: controller.seek_relative(-cec_seek_frames); break;
    }
    const auto after = controller.state();
    return before.playback != after.playback || before.track != after.track ||
           before.position_lba != after.position_lba;
}
}
void run_player_session(const std::string& device, CddaBackend backend,
                        const std::string& audio_device, bool use_cec, const std::string& cec_device,
                        bool cec_diagnostics, bool interactive, bool metadata_enabled,
                        const std::string& metadata_cache, const std::string& api_listen,
                        int api_port) {
#ifndef ENABLE_METADATA
    (void)metadata_enabled; (void)metadata_cache;
#endif
#ifndef ENABLE_API
    (void)api_listen; (void)api_port;
#endif
    Signals signals; // Worker inherits the blocked signal mask.
    PlayerController controller;
    auto audio = make_alsa_output(audio_device);
    PcmWorker worker([=] { return make_cdda_reader(backend, device); });
    MediaWorker media_worker(
        [device] { return read_cd_media(device).observation; },
        [device] { return read_cd_toc(device); });
#ifdef ENABLE_METADATA
    std::unique_ptr<MetadataWorker> metadata_worker;
    if (metadata_enabled) {
        MetadataOptions options{metadata_cache, true};
        metadata_worker = std::make_unique<MetadataWorker>(
            [options](const DiscToc& toc) { return lookup_musicbrainz_disc(toc, options); });
    }
#endif
    PlaybackEngine engine(controller, worker, *audio, 0);
    struct StopOnExit {
        PlayerController& controller; PcmWorker& worker; AudioOutput& output;
        ~StopOnExit() {
            controller.stop(); worker.cancel();
            try { output.reset(); } catch (...) {}
        }
    } stop_on_exit{controller, worker, *audio};
    CecDevice cec(cec_device, cec_diagnostics);
    std::cout << "player: backend=" << (backend == CddaBackend::direct ? "direct" : "paranoia")
              << " audio=" << audio_device << " PCM=44100Hz/stereo/S16_native"
              << " stdin_commands=" << (interactive ? "enabled" : "disabled") << '\n';
    if (interactive)
        std::cout << "Commands: play pause stop next previous track N seek SECONDS state quit\n";
    print_state(controller);
    MediaStateTracker media_state;
    std::optional<DiscToc> loaded_toc;
    bool toc_pending = false;
    bool toc_needs_refresh = true;
#ifdef ENABLE_METADATA
    MetadataSession metadata_session;
#endif
#ifdef ENABLE_API
    std::string api_state_json;
    std::uint64_t api_revision = 0;
    auto publish_api_snapshot = [&] {
        MetadataResult metadata;
#ifdef ENABLE_METADATA
        if (metadata_enabled) metadata = metadata_session.snapshot();
#endif
        api_state_json = serialize_daemon_snapshot(make_daemon_snapshot(
            ++api_revision, controller.state(), media_state.state(), loaded_toc, metadata));
    };
    publish_api_snapshot();
    std::unique_ptr<ApiServer> api_server;
    if (api_port) {
        api_server = std::make_unique<ApiServer>(api_listen, api_port,
                                                 [&] { return api_state_json; });
        std::cout << "api: listening=http://";
        if (api_listen.find(':') != std::string::npos) std::cout << '[' << api_listen << ']';
        else std::cout << api_listen;
        std::cout << ':' << api_port;
        if (api_listen != "127.0.0.1" && api_listen != "::1")
            std::cout << " access=external-debug";
        std::cout << '\n' << std::flush;
    }
    auto next_api_snapshot = std::chrono::steady_clock::now();
#endif
    std::string last_media_error;
    std::string input;
    auto next_cec = std::chrono::steady_clock::now();
    auto next_media = std::chrono::steady_clock::now();
    bool quitting = false;
    while (!quitting) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_media) {
            // Keep status ioctls off the drive while CD-DA reads are active.
            // A read failure stops playback; polling then resumes and observes
            // an opened tray or removed disc.
            if (controller.state().playback != PlaybackState::playing)
                (void)media_worker.request(MediaWork::observe);
            next_media = now + std::chrono::milliseconds(500);
        }
        MediaWorkerResult media_result{};
        while (media_worker.pop(media_result)) {
            if (!media_result.error.empty()) {
                if (media_result.error != last_media_error)
                    std::cerr << "media: " << media_result.error << '\n';
                last_media_error = media_result.error;
                if (media_result.work == MediaWork::read_toc) toc_pending = false;
                continue;
            }
            last_media_error.clear();
            if (media_result.work == MediaWork::observe) {
                const auto before = media_state.state();
                const auto after = media_state.observe(*media_result.observation);
                if (after != before)
                    std::cout << "media: state=" << media_state_name(after) << '\n' << std::flush;
                if (after == MediaLifecycleState::loading) {
#ifdef ENABLE_METADATA
                    if (metadata_worker) metadata_worker->cancel_pending();
                    metadata_session.invalidate();
#endif
                    toc_pending = false;
                    toc_needs_refresh = true;
                } else if (after == MediaLifecycleState::no_disc ||
                           after == MediaLifecycleState::unsupported) {
                    toc_pending = false;
                    toc_needs_refresh = true;
                    loaded_toc.reset();
#ifdef ENABLE_METADATA
                    if (metadata_worker) metadata_worker->cancel_pending();
                    metadata_session.invalidate();
#endif
                    if (controller.state().playback != PlaybackState::no_disc) {
                        controller.remove_disc();
                        engine.set_disc_end(0);
                        engine.synchronize();
                        worker.discard_reader();
                        print_state(controller);
                    }
                } else if (!toc_pending && (toc_needs_refresh || !loaded_toc ||
                           controller.state().playback == PlaybackState::no_disc)) {
                    toc_pending = media_worker.request(MediaWork::read_toc);
                }
            } else {
                toc_pending = false;
                if (media_state.state() != MediaLifecycleState::audio_ready || !media_result.toc)
                    continue;
                if (!loaded_toc || !same_toc(*loaded_toc, *media_result.toc) ||
                    controller.state().playback == PlaybackState::no_disc) {
                    controller.load_disc(*media_result.toc);
                    engine.set_disc_end(media_result.toc->leadout_lba);
                    engine.synchronize();
                    loaded_toc = *media_result.toc;
                    std::cout << "media: audio_disc tracks=" << loaded_toc->tracks.size()
                              << " leadout_lba=" << loaded_toc->leadout_lba << '\n' << std::flush;
                    print_state(controller);
#ifdef ENABLE_METADATA
                    if (metadata_worker) {
                        const auto request = metadata_session.begin(*loaded_toc);
                        std::cout << "metadata: status=LOADING generation=" << request.generation << '\n' << std::flush;
                        metadata_worker->request(request);
                    }
#endif
                }
                toc_needs_refresh = false;
            }
        }
#ifdef ENABLE_METADATA
        if (metadata_worker) {
            MetadataWorkerResult result{};
            while (metadata_worker->pop(result)) {
                const auto result_generation = result.generation;
                if (!metadata_session.apply(std::move(result))) {
                    std::cout << "metadata: stale_result_discarded generation=" << result_generation << '\n' << std::flush;
                    continue;
                }
                const auto& metadata = metadata_session.snapshot();
                std::cout << "metadata: status=" << metadata_status_name(metadata.status)
                          << " candidates=" << metadata.candidates.size()
                          << " disc_id=" << metadata.disc_id
                          << " cache=" << (metadata.from_cache ? "hit" : "miss");
                if (!metadata.error.empty()) std::cout << " error=\"" << metadata.error << '"';
                std::cout << '\n' << std::flush;
            }
        }
#endif
        if (use_cec && now >= next_cec) {
            const auto update_started = std::chrono::steady_clock::now();
            cec.update();
            const auto update_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - update_started).count();
            if (cec_diagnostics && update_us >= 10'000)
                std::cout << "cec: update_us=" << update_us << '\n' << std::flush;
            next_cec = now + std::chrono::milliseconds(250);
        }
        try { engine.tick(); }
        catch (const std::exception& error) {
            std::cerr << "player: playback stopped: " << error.what() << '\n';
            print_state(controller);
        }
#ifdef ENABLE_API
        if (api_server) {
            if (now >= next_api_snapshot) {
                publish_api_snapshot();
                next_api_snapshot = now + std::chrono::milliseconds(250);
            }
            api_server->service();
        }
#endif
        pollfd fds[]{{signals.fd, POLLIN, 0}, {interactive ? STDIN_FILENO : -1, POLLIN, 0},
                     {use_cec ? cec.poll_fd() : -1, POLLIN, 0}};
        const auto result = poll(fds, 3, 10);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "player poll");
        }
        if (fds[0].revents & POLLIN) {
            signalfd_siginfo info{};
            (void)read(signals.fd, &info, sizeof(info));
            std::cout << "player: shutdown signal=" << info.ssi_signo << '\n';
            break;
        }
        if (fds[2].revents & (POLLERR | POLLHUP | POLLNVAL))
            throw std::runtime_error("CEC poll failure");
        if (fds[2].revents & POLLIN) {
            // Drain every queued message now. Processing only one per poll
            // iteration could make a key wait behind unrelated CEC traffic.
            for (;;) {
                const auto received = cec.receive();
                if (!received.dequeued) break;
                if (received.command && apply_cec_command(controller, *received.command)) {
                    engine.synchronize();
                    print_state(controller);
                }
            }
        }
        if (fds[1].revents & (POLLERR | POLLNVAL)) throw std::runtime_error("stdin poll failure");
        if (!(fds[1].revents & (POLLIN | POLLHUP))) continue;
        char buffer[512];
        const auto n = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; throw std::runtime_error("stdin read failed"); }
        input.append(buffer, static_cast<std::size_t>(n));
        if (input.size() > 4096) { input.clear(); std::cerr << "player: input too long\n"; continue; }
        std::size_t newline;
        while ((newline = input.find('\n')) != std::string::npos) {
            auto command = input.substr(0, newline); input.erase(0, newline + 1);
            if (!command.empty() && command.back() == '\r') command.pop_back();
            bool changed = true;
            if (command == "quit") { quitting = true; break; }
            if (command == "state") { print_state(controller); continue; }
            if (command == "play") {
                if (controller.state().playback == PlaybackState::playing) changed = false;
                else controller.play();
            } else if (command == "pause") {
                if (controller.state().playback != PlaybackState::playing) changed = false;
                else controller.pause();
            } else if (command == "stop") controller.stop();
            else if (command == "next") controller.next();
            else if (command == "previous") controller.previous();
            else if (command.starts_with("track ") || command.starts_with("seek ")) {
                const auto space = command.find(' ');
                const auto value = std::string_view(command).substr(space + 1);
                int number = 0;
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
                if (error != std::errc{} || end != value.data() + value.size()) changed = false;
                else if (command.starts_with("track ")) changed = controller.select_track(number);
                else controller.seek_relative(std::int64_t(number) * cd_frames_per_second);
                if (!changed) std::cerr << "player: invalid argument\n";
            } else { changed = false; std::cerr << "player: unknown command\n"; }
            if (changed) engine.synchronize();
            print_state(controller);
        }
    }
    controller.stop();
    engine.synchronize();
    std::cout << "player: output stopped; waiting for outstanding drive I/O\n" << std::flush;
    // worker joins before audio/signals destruction. No detached hardware access.
}
