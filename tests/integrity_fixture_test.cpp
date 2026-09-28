#include "integrity_fixture.hpp"
#include "pcm_worker.hpp"

#include <cerrno>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
void check_impl(bool value, int line) {
    if (!value) throw std::runtime_error("integrity fixture test failed at line " + std::to_string(line));
}
#define check(value) check_impl((value), __LINE__)

template<class F>
void wait_for(F condition) {
    for (unsigned i = 0; i < 1000; ++i) {
        if (condition()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("timed out waiting for worker");
}

template<class F>
void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid scripted operation accepted");
}

ScriptedRead step(std::int32_t lba, std::size_t requested, std::size_t read,
                  ReadStatus status = ReadStatus::ok, int error = 0,
                  unsigned retries = 0, ParanoiaEvents events = {},
                  LocalReadVerification verification = {}) {
    ReadResult result{lba, requested, read, status, error, retries};
    result.paranoia = events;
    result.verification = verification;
    return {result, 1};
}

PcmWorker make_worker(std::vector<ScriptedRead> script) {
    return PcmWorker([script = std::move(script)] {
        return std::make_unique<ScriptedCddaReader>(script);
    }, "fixture", {15, 15});
}
}

int main() {
    try {
        {
            ScriptedCddaReader reader({step(100, 15, 15)});
            std::vector<std::int16_t> pcm(15 * cdda_samples_per_frame);
            rejects([&] { reader.read(pcm); });
            rejects([&] { reader.seek(99); });
            reader.seek(100);
            rejects([&] { reader.read(std::span<std::int16_t>(pcm).subspan(1)); });
            check(reader.read(pcm).frames_read == 15);
            rejects([&] { reader.read(pcm); });
        }
        {
            ScriptedCddaReader reader({
                step(200, 15, 0, ReadStatus::read_error, EIO), step(200, 15, 15),
            });
            std::vector<std::int16_t> pcm(15 * cdda_samples_per_frame);
            reader.seek(200);
            check(reader.read(pcm).status == ReadStatus::read_error);
            rejects([&] { reader.read(pcm); });
            reader.seek(200);
            check(reader.read(pcm).frames_read == 15);
        }
        {
            LocalReadVerification matched;
            matched.attempts = 3;
            matched.complete_reads = 3;
            matched.matching_reads = 2;
            matched.mismatches = 1;
            matched.accepted_candidate = 2;
            matched.accepted_attempt = 3;
            auto worker = make_worker({
                step(0, 15, 15),
                step(15, 15, 15, ReadStatus::ok, 0, 1),
                step(30, 15, 15, ReadStatus::ok, 0, 0, {0, 1, 1}),
                step(45, 15, 15, ReadStatus::ok, 0, 0, {}, matched),
            });
            worker.set_disc_generation(7);
            worker.start(0, 60);
            std::vector<IntegrityReadStatus> statuses;
            PcmBlock block;
            for (unsigned i = 0; i < 4; ++i) {
                wait_for([&] { return worker.pop(block); });
                statuses.push_back(block.evidence.status);
                check(block.evidence.disc_generation == 7);
            }
            wait_for([&] { return worker.status().done; });
            check((statuses == std::vector<IntegrityReadStatus>{
                IntegrityReadStatus::clean, IntegrityReadStatus::uncertain,
                IntegrityReadStatus::recovered, IntegrityReadStatus::recovered}));
            const auto diagnostics = worker.status(true).diagnostics;
            check(diagnostics.stats.read_calls == 4 && diagnostics.stats.frames_accepted == 60);
            check(diagnostics.stats.direct_retries == 1 && diagnostics.stats.backend_fixups == 1);
            check(diagnostics.stats.verified_calls == 1 && diagnostics.stats.verification_mismatches == 1);
            check(diagnostics.active_warning && diagnostics.active_warning->start_lba == 15);
            check(diagnostics.recent_reads.size() == 4 && diagnostics.disc_map);
            check(diagnostics.disc_map->size == 4);
            PlayerEvent event;
            for (const auto expected : statuses) {
                check(worker.pop_event(event));
                check(event.read.status == expected);
            }
            check(!worker.pop_event(event));
        }
        {
            auto worker = make_worker({step(300, 15, 0, ReadStatus::read_error, EIO, 1)});
            worker.set_disc_generation(8);
            worker.start(300, 315);
            wait_for([&] { return !worker.status().error.empty(); });
            const auto diagnostics = worker.status(true).diagnostics;
            check(diagnostics.activity == ReadActivity::failed && diagnostics.stats.failed_calls == 1);
            check(diagnostics.latest && diagnostics.latest->status == IntegrityReadStatus::uncertain);
            check(diagnostics.active_warning && diagnostics.active_warning->start_lba == 300);
            check(diagnostics.disc_map && diagnostics.disc_map->size == 1);
            check(!(diagnostics.disc_map->regions[0].flags & DiscReadMap::accepted));
            check(diagnostics.disc_map->regions[0].flags & DiscReadMap::uncertain);
        }
        {
            std::vector<ScriptedRead> script;
            for (int i = 0; i < 300; ++i) script.push_back(step(i * 15, 15, 15));
            auto worker = make_worker(std::move(script));
            worker.set_disc_generation(9);
            worker.start(0, 15 * 300);
            PcmBlock block;
            for (unsigned i = 0; i < 300; ++i)
                wait_for([&] { return worker.pop(block); });
            wait_for([&] { return worker.status().done; });
            const auto diagnostics = worker.status(true).diagnostics;
            check(diagnostics.recent_reads.size() == read_history_capacity);
            check(diagnostics.history_evicted == 172 && diagnostics.dropped_events == 44);
            check(diagnostics.recent_reads.front().read_sequence == 173);
            check(diagnostics.coverage.accepted_unique_frames == 4500);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
