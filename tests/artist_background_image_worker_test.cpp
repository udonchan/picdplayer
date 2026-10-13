#include "artist_background_image_worker.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

#define check(value) do { if (!(value)) throw std::runtime_error("image worker test failed at line " + std::to_string(__LINE__)); } while (0)

namespace {
constexpr auto artist_a = "0383dadf-2a4e-4d10-a46a-e9e041da8eb3";
constexpr auto artist_b = "89ad4ac3-39f7-470e-963a-56509c546378";
const std::string jpeg = std::string("\xff\xd8", 2) + "synthetic" + std::string("\xff\xd9", 2);
ArtistBackgroundImageRequest request(std::uint64_t generation, std::string artist = artist_a,
                                     std::string id = "123") {
    const auto url = "https://assets.fanart.tv/fanart/music/" + artist + "/artistbackground/fixture.jpg";
    return {generation, std::move(artist), {std::move(id), url, 1920, 1080}};
}
template<class Predicate> void await(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
ArtistBackgroundImageWorkerResult pop(ArtistBackgroundImageWorker& worker) {
    ArtistBackgroundImageWorkerResult result;
    await([&] { return worker.pop(result); });
    return result;
}
}

int main() {
    try {
        bool rejected = false;
        try { ArtistBackgroundImageWorker empty({}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);

        std::atomic<unsigned> http_calls = 0;
        ArtistBackgroundImageWorker downloader([&](const auto& input, const auto& cancelled) {
            // Empty cache path exercises transient bytes only, not a disk policy.
            return load_artist_background_image(input.artist_mbid, input.candidate, {}, cancelled,
                [&](std::string_view url, std::size_t limit, const auto&) {
                    check(url == input.candidate.url && limit == artist_background_image_limit);
                    ++http_calls;
                    HttpResponse response;
                    response.status = 200;
                    response.content_type = "image/jpeg";
                    response.body = jpeg;
                    return response;
                });
        });
        downloader.request(request(1));
        auto result = pop(downloader);
        check(result.request.generation == 1 && result.request.artist_mbid == artist_a &&
              result.request.candidate.id == "123" && result.image && result.image->bytes == jpeg);
        downloader.request(request(2));
        check(pop(downloader).image.has_value() && http_calls == 2);
        check(!downloader.pop(result));
        auto hostile = request(3);
        hostile.candidate.url = "https://localhost/fixture.jpg";
        downloader.request(hostile);
        check(!pop(downloader).image && http_calls == 2);
        auto too_large = request(4);
        too_large.candidate.url = std::string(4097, 'x');
        rejected = false;
        try { downloader.request(std::move(too_large)); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);

        // The active task deliberately ignores cancellation until released.
        // All intervening requests must collapse to the latest pending request.
        std::atomic<bool> started = false, release = false;
        std::atomic<unsigned> tasks = 0;
        ArtistBackgroundImageWorker latest([&](const auto& input, const auto&) {
            ++tasks;
            if (input.generation == 1) {
                started = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!release && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return std::optional<ArtistBackgroundImage>{{"image/jpeg", jpeg}};
        });
        latest.request(request(1));
        await([&] { return started.load(); });
        for (unsigned i = 2; i <= 100; ++i) latest.request(request(i, artist_b));
        release = true;
        result = pop(latest);
        check(result.request.generation == 100 && result.request.artist_mbid == artist_b && result.image && tasks == 2);
        check(!latest.pop(result));

        // Same generation: a changed artist or candidate must replace the old result too.
        latest.request(request(100, artist_a, "456"));
        result = pop(latest);
        check(result.request.generation == 100 && result.request.artist_mbid == artist_a &&
              result.request.candidate.id == "456");

        started = false;
        release = false;
        ArtistBackgroundImageWorker same_generation([&](const auto& input, const auto&) {
            if (input.artist_mbid == artist_a) {
                started = true;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!release && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return std::optional<ArtistBackgroundImage>{{"image/jpeg", jpeg}};
        });
        same_generation.request(request(42));
        await([&] { return started.load(); });
        same_generation.request(request(42, artist_b, "789"));
        release = true;
        result = pop(same_generation);
        check(result.request.generation == 42 && result.request.artist_mbid == artist_b &&
              result.request.candidate.id == "789" && result.image);
        check(!same_generation.pop(result));

        std::atomic<bool> cancel_started = false, cancelled_seen = false;
        ArtistBackgroundImageWorker cancelling([&](const auto&, const auto& cancelled) {
            cancel_started = true;
            while (!cancelled()) std::this_thread::yield();
            cancelled_seen = true;
            return std::optional<ArtistBackgroundImage>{{"image/jpeg", jpeg}};
        });
        cancelling.request(request(1));
        await([&] { return cancel_started.load(); });
        cancelling.request(request(2));
        cancelling.cancel_pending();
        await([&] { return cancelled_seen.load(); });
        check(!cancelling.pop(result));

        std::atomic<bool> closing_started = false, closing_seen = false;
        {
            ArtistBackgroundImageWorker closing([&](const auto&, const auto& cancelled) {
                closing_started = true;
                while (!cancelled()) std::this_thread::yield();
                closing_seen = true;
                return std::optional<ArtistBackgroundImage>{};
            });
            closing.request(request(1));
            await([&] { return closing_started.load(); });
        }
        check(closing_seen);

        ArtistBackgroundImageWorker failures([](const auto& input, const auto&) -> std::optional<ArtistBackgroundImage> {
            switch (input.generation) {
            case 1: throw std::runtime_error("synthetic failure");
            case 2: return std::nullopt;
            case 3: return ArtistBackgroundImage{"image/jpeg", std::string(artist_background_image_limit + 1, 'x')};
            case 4: return ArtistBackgroundImage{"text/html", jpeg};
            case 5: return ArtistBackgroundImage{"image/jpeg", {}};
            case 7: return ArtistBackgroundImage{"image/jpeg", std::string(artist_background_image_limit, 'x')};
            default: return ArtistBackgroundImage{"image/jpeg", jpeg};
            }
        });
        for (unsigned i = 1; i <= 7; ++i) {
            failures.request(request(i));
            result = pop(failures);
            check(result.request.generation == i && result.image.has_value() == (i >= 6));
            if (i == 7) check(result.image->bytes.size() == artist_background_image_limit);
        }
        std::cout << "PASS: transient image helper, provenance, replacement, cancellation, shutdown and result bounds\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
