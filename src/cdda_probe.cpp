#include "cdda_probe.hpp"
#include "cd_device.hpp"
#include "pcm_file.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

void probe_cdda(const std::string& device, CddaBackend backend, int number, int frames, unsigned retries, const std::string& output) {
    require_cdda_backend(backend);
    if (frames < 1 || frames > 750) throw std::invalid_argument("frames must be 1..750");
    const auto toc = read_cd_toc(device);
    const auto track = std::find_if(toc.tracks.begin(), toc.tracks.end(),
        [number](const Track& t) { return t.number == number; });
    if (track == toc.tracks.end() || frames > track->length_frames)
        throw std::invalid_argument("requested range is outside track");
    using Clock = std::chrono::steady_clock;
    auto begin = Clock::now();
    auto reader = make_cdda_reader(backend, device, {retries});
    const auto micros = [](auto start) {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    };
    std::cout << "cdda: backend=direct device=" << device << " open_us=" << micros(begin)
              << " retries_limit=" << retries << " input_byte_order=little (unverified on device)\n";
    begin = Clock::now();
    reader->seek(track->start_lba);
    std::cout << "cdda: seek_lba=" << track->start_lba << " seek_us=" << micros(begin) << '\n';
    const auto start = Clock::now();
    int remaining = frames;
    std::vector<std::int16_t> pcm(15 * cdda_samples_per_frame);
    std::vector<std::int16_t> captured;
    if (!output.empty()) captured.reserve(frames * cdda_samples_per_frame);
    while (remaining > 0) {
        const auto count = std::min(15, remaining);
        begin = Clock::now();
        const auto result = reader->read(std::span(pcm).first(count * cdda_samples_per_frame));
        std::cout << "cdda: lba=" << result.start_lba << " requested=" << result.frames_requested
                  << " read=" << result.frames_read << " read_us=" << micros(begin)
                  << " retries=" << result.retries << " errno=" << result.native_error
                  << " status=" << (result.status == ReadStatus::ok ? "ok" : "read_error") << '\n' << std::flush;
        if (result.status != ReadStatus::ok) throw std::runtime_error("CDDA read failed; PCM discarded");
        if (remaining == frames) std::cout << "cdda: first_block_us=" << micros(start) << '\n';
        if (!output.empty()) captured.insert(captured.end(), pcm.begin(),
            pcm.begin() + result.frames_read * cdda_samples_per_frame);
        remaining -= static_cast<int>(result.frames_read);
    }
    const auto read_elapsed = micros(start);
    if (!output.empty()) {
        save_pcm_s16le(output, captured);
        std::cout << "cdda: saved=" << output << " bytes=" << captured.size() * 2
                  << " format=S16_LE rate=44100 channels=2\n";
    }
    std::cout << "cdda: completed frames=" << frames << " elapsed_us=" << read_elapsed
              << (output.empty() ? " (PCM discarded; no playback)\n" : " (PCM saved; no playback)\n");
}
