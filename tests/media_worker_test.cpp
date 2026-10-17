#include "media_worker.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <future>

void check(bool value) {
    if (!value) throw std::runtime_error("media worker test failed");
}
struct Gate {
    std::mutex mutex; std::condition_variable changed; bool entered=false, open=false;
    void pass() {std::unique_lock lock(mutex);entered=true;changed.notify_all();
        if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return open;}))throw std::runtime_error("media gate timeout");}
    void reached() {std::unique_lock lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
    void release() {std::lock_guard lock(mutex);open=true;changed.notify_all();}
};
struct ReleaseGate {Gate& gate;~ReleaseGate(){gate.release();}};
template<class F> void wait_until(F predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do {if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("media idle timeout");
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
        bool drive_started = false;
        bool drive_stopped = false;
        bool drive_speed_set = false;
        MediaWorker worker(
            std::make_shared<DriveAccessCoordinator>(),
            [] { return MediaObservation::audio_disc; },
            [toc] { return toc; },
            [] {},
            [] { DriveCapabilities result; result.device = "/dev/fake"; return result; },
            [&] { drive_started = true; },
            [&] { drive_speed_set = true; },
            [&] { drive_stopped = true; });

        check(worker.request(MediaWork::probe_drive));
        auto result = wait_for_result(worker);
        check(result.work == MediaWork::probe_drive && result.drive);
        check(result.drive->device == "/dev/fake" && result.error.empty());

        check(worker.request(MediaWork::observe));
        result = wait_for_result(worker);
        check(result.work == MediaWork::observe && result.observation == MediaObservation::audio_disc);
        check(!result.toc && result.error.empty());

        check(worker.request(MediaWork::read_toc));
        result = wait_for_result(worker);
        check(result.work == MediaWork::read_toc && result.toc && result.toc->tracks.size() == 2);
        check(!result.observation && result.error.empty());

        check(worker.request(MediaWork::start_drive));
        result = wait_for_result(worker);
        check(result.work == MediaWork::start_drive && drive_started && result.error.empty());

        check(worker.request(MediaWork::stop_drive));
        result = wait_for_result(worker);
        check(result.work == MediaWork::stop_drive && drive_stopped && result.error.empty());

        check(worker.request(MediaWork::set_drive_speed));
        result = wait_for_result(worker);
        check(result.work == MediaWork::set_drive_speed && drive_speed_set && result.error.empty());

        check(worker.request(MediaWork::eject));
        result = wait_for_result(worker);
        check(result.work == MediaWork::eject && !result.observation && !result.toc && result.error.empty());

        MediaWorker failing(
            std::make_shared<DriveAccessCoordinator>(),
            []() -> MediaObservation { throw std::runtime_error("probe failed"); },
            [toc] { return toc; },
            [] {});
        check(failing.request(MediaWork::observe));
        result = wait_for_result(failing);
        check(!result.observation && result.error == "probe failed");

        bool rejected = false;
        try { MediaWorker invalid(std::make_shared<DriveAccessCoordinator>(), {}, [toc] { return toc; }, [] {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        {
            Gate callback;
            auto access=std::make_shared<DriveAccessCoordinator>();
            MediaWorker delayed(access,[]{return MediaObservation::audio_disc;},[&]{callback.pass();return toc;},[]{});
            ReleaseGate release{callback};
            delayed.invalidate(1);
            check(delayed.request(MediaWork::read_toc));callback.reached();
            auto invalidation=std::async(std::launch::async,[&]{delayed.invalidate(2);return delayed.retiring();});
            const bool nonblocking=invalidation.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
            if(!nonblocking)callback.release();
            check(invalidation.get()&&nonblocking);
            callback.release();wait_until([&]{return delayed.idle();});
            MediaWorkerResult stale{};check(!delayed.pop(stale));
            check(delayed.request(MediaWork::observe));const auto fresh=wait_for_result(delayed);
            check(fresh.observation_generation==2&&fresh.observation==MediaObservation::audio_disc);
        }
        {
            Gate held;
            auto access=std::make_shared<DriveAccessCoordinator>();
            auto holder=std::async(std::launch::async,[&]{access->invoke([&]{held.pass();});});
            held.reached();
            std::atomic<unsigned> checked{0},ejected{0};
            std::atomic<std::uint64_t> observed{1};
            MediaWorker guarded(access,[]{return MediaObservation::audio_disc;},[toc]{return toc;},[&]{++ejected;},
                {},{},{},{},[&](std::uint64_t generation){const bool valid=generation==observed.load();++checked;return valid;});
            ReleaseGate release{held};
            guarded.invalidate(1);check(guarded.request(MediaWork::eject));
            wait_until([&]{return checked.load()!=0;});
            guarded.invalidate(2);observed=2;check(guarded.retiring());
            held.release();holder.get();wait_until([&]{return guarded.idle();});
            check(ejected==0);MediaWorkerResult stale{};check(!guarded.pop(stale));
            check(guarded.request(MediaWork::observe));check(wait_for_result(guarded).observation_generation==2);
            bool backwards=false;try{guarded.invalidate(1);}catch(const std::invalid_argument&){backwards=true;}
            check(backwards);
        }
        {
            std::atomic<unsigned> ejected{0};
            MediaWorker guarded(std::make_shared<DriveAccessCoordinator>(),[]{return MediaObservation::no_disc;},
                                [toc]{return toc;},[&]{++ejected;});
            check(guarded.request(static_cast<MediaWork>(99)));
            check(!wait_for_result(guarded).error.empty()&&ejected==0);
        }
        {
            std::atomic<unsigned> checked{0},ejected{0};
            MediaWorker skipped(std::make_shared<DriveAccessCoordinator>(),[]{return MediaObservation::no_disc;},
                [toc]{return toc;},[&]{++ejected;},{},{},{},{},[&](std::uint64_t){return ++checked>1;});
            check(skipped.request(MediaWork::eject));wait_until([&]{return skipped.idle();});
            MediaWorkerResult phantom{};check(!skipped.pop(phantom)&&ejected==0);
        }
        std::cout << "PASS: asynchronous media observation and TOC work\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
