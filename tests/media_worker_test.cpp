#include "media_worker.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value) {
    if (!value) throw std::runtime_error("media worker test failed");
}

MediaWorkerResult wait_for_result(MediaWorker& worker) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    MediaWorkerResult result{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.pop(result)) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("media worker result timeout");
}

int main() {
    try {
        const auto toc = make_audio_toc(1, std::vector<std::int32_t>{0, 750}, 1500);
        MediaWorker worker(
            [] { return MediaObservation::audio_disc; },
            [toc] { return toc; },
            [] {});

        check(worker.request(MediaWork::observe));
        auto result = wait_for_result(worker);
        check(result.work == MediaWork::observe && result.observation == MediaObservation::audio_disc);
        check(!result.toc && result.error.empty());

        check(worker.request(MediaWork::read_toc));
        result = wait_for_result(worker);
        check(result.work == MediaWork::read_toc && result.toc && result.toc->tracks.size() == 2);
        check(!result.observation && result.error.empty());

        check(worker.request(MediaWork::eject));
        result = wait_for_result(worker);
        check(result.work == MediaWork::eject && !result.observation && !result.toc && result.error.empty());

        MediaWorker failing(
            []() -> MediaObservation { throw std::runtime_error("probe failed"); },
            [toc] { return toc; },
            [] {});
        check(failing.request(MediaWork::observe));
        result = wait_for_result(failing);
        check(!result.observation && result.error == "probe failed");

        bool rejected = false;
        try { MediaWorker invalid({}, [toc] { return toc; }, [] {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        std::cout << "PASS: asynchronous media observation and TOC work\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
