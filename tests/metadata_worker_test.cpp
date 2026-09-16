#include "metadata_worker.hpp"
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
        MetadataWorker worker([](const DiscToc& toc) {
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
        std::cout << "PASS: metadata worker latest request and cancellation\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
