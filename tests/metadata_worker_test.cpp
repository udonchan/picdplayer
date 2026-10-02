#include "metadata_worker.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace { void check(bool value) { if (!value) throw std::runtime_error("metadata worker test failed"); } }
int main() {
    try {
        const auto toc_a = make_audio_toc(1, std::vector<std::int32_t>{0}, 75);
        const auto toc_b = make_audio_toc(1, std::vector<std::int32_t>{0, 75}, 150);
        MetadataWorker worker([](const DiscToc& toc, const MetadataWorker::Cancelled&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            MetadataResult result; result.status = MetadataStatus::available;
            result.disc_id = std::to_string(toc.tracks.size()); return result;
        });
        worker.request({1, toc_a}); worker.request({2, toc_b});
        MetadataWorkerResult result{};
        for (int i = 0; i < 100 && !worker.pop(result); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(result.generation == 2 && result.metadata.disc_id == "2");
        worker.request({3, toc_a}); worker.cancel_pending();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(!worker.pop(result));

        std::atomic<bool> lookup_started = false;
        auto shutdown_started = std::chrono::steady_clock::now();
        {
            MetadataWorker cancelling([&](const DiscToc&, const MetadataWorker::Cancelled& cancelled) {
                lookup_started.store(true);
                while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return MetadataResult{};
            });
            cancelling.request({1, toc_a});
            for (int i = 0; i < 100 && !lookup_started.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            check(lookup_started.load());
            shutdown_started = std::chrono::steady_clock::now();
        }
        check(std::chrono::steady_clock::now() - shutdown_started < std::chrono::milliseconds(500));

        bool rejected_empty_callback = false;
        try { MetadataWorker invalid({}); }
        catch (const std::invalid_argument&) { rejected_empty_callback = true; }
        check(rejected_empty_callback);

        MetadataWorker failing([](const DiscToc&, const MetadataWorker::Cancelled&) -> MetadataResult {
            throw std::runtime_error("bounded metadata input rejected");
        });
        failing.request({4, toc_a});
        for (int i = 0; i < 100 && !failing.pop(result); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(result.generation == 4 && result.metadata.status == MetadataStatus::error &&
              result.metadata.error == "bounded metadata input rejected");
        ArtworkWorker artwork([](const std::string& release, const MetadataWorker::Cancelled&) {
            ArtworkInfo info;
            info.status = ArtworkStatus::available;
            info.image_url = release;
            return info;
        });
        artwork.request({5, "release-a"});
        ArtworkWorkerResult artwork_result{};
        for (int i = 0; i < 100 && !artwork.pop(artwork_result); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(artwork_result.request.generation == 5 && artwork_result.request.release_id == "release-a" &&
              artwork_result.artwork.status == ArtworkStatus::available);
        bool rejected_empty_artwork_callback = false;
        try { ArtworkWorker invalid({}); }
        catch (const std::invalid_argument&) { rejected_empty_artwork_callback = true; }
        check(rejected_empty_artwork_callback);
        std::atomic<bool> first_artwork_started = false;
        ArtworkWorker replacing([&](const std::string& release, const MetadataWorker::Cancelled& cancelled) {
            if (release == "old") {
                first_artwork_started = true;
                while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            ArtworkInfo info; info.status = ArtworkStatus::available; info.image_url = release;
            return info;
        });
        replacing.request({6, "old"});
        for (int i = 0; i < 100 && !first_artwork_started; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        check(first_artwork_started);
        replacing.request({6, "new"});
        for (int i = 0; i < 100 && !replacing.pop(artwork_result); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(artwork_result.request.release_id == "new" && artwork_result.artwork.image_url == "new");
        std::cout << "PASS: metadata worker latest request and cancellation\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
