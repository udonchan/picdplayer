#include "artist_background_worker.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool okay) { if (!okay) throw std::runtime_error("artist background worker test failed"); }
}

int main() {
    try {
        bool empty_rejected = false;
        try { ArtistBackgroundWorker empty({}); }
        catch (const std::invalid_argument&) { empty_rejected = true; }
        check(empty_rejected);

        std::atomic<bool> old_started = false;
        ArtistBackgroundWorker worker([&](const std::string& mbid,
                                          const ArtistBackgroundWorker::Cancelled& cancelled) {
            if (mbid == "old") {
                old_started = true;
                for (int i = 0; i < 500 && !cancelled(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return ArtistBackgroundLookup{ArtistBackgroundLookupStatus::available, {}};
            }
            return ArtistBackgroundLookup{ArtistBackgroundLookupStatus::unavailable, {}};
        });
        worker.request({1, "old"});
        for (int i = 0; i < 500 && !old_started; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(old_started);
        worker.request({2, "new"});
        ArtistBackgroundWorkerResult result;
        for (int i = 0; i < 500 && !worker.pop(result); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        check(result.request.generation == 2 && result.request.artist_mbid == "new" &&
              result.lookup.status == ArtistBackgroundLookupStatus::unavailable);
        worker.request({3, "new"});
        worker.cancel_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(!worker.pop(result));
        std::cout << "PASS: artist background worker discards replaced and cancelled results\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
