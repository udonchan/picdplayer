#include "linux_ioctl_reader.hpp"
#include <algorithm>
#include <cerrno>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
void check(bool ok) { if (!ok) throw std::runtime_error("CDDA check failed"); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid operation accepted");
}
int main() {
    try {
        check(parse_cdda_backend("direct") == CddaBackend::direct);
        rejects([] { parse_cdda_backend("typo"); });
        rejects([] { make_cdda_reader(CddaBackend::paranoia, "/nonexistent"); });
        std::vector<int> positions;
        LinuxIoctlReader reader([&](int lba, auto buffer) {
            positions.push_back(lba);
            std::fill(buffer.begin(), buffer.end(), 123);
            return 0;
        });
        CddaReader& common = reader;
        std::vector<std::int16_t> pcm(76 * cdda_samples_per_frame, -1);
        rejects([&] { common.read(pcm); });
        common.seek(150);
        auto result = common.read(pcm);
        check(result.frames_read == 76 && result.start_lba == 150);
        check(positions == std::vector<int>({150,225}));
        check(pcm.back() == 123);
        check(common.read(std::span(pcm).first(cdda_samples_per_frame)).start_lba == 226);
        rejects([&] { common.seek(-1); });
        rejects([&] { common.read(std::span(pcm).first(1)); });
        rejects([&] { common.read({}); });
        common.seek(std::numeric_limits<std::int32_t>::max());
        rejects([&] { common.read(pcm); });
        int calls = 0;
        LinuxIoctlReader partial([&](int, auto buffer) {
            std::fill(buffer.begin(), buffer.end(), 99);
            return ++calls == 1 ? 0 : EIO;
        });
        std::fill(pcm.begin(), pcm.end(), -1);
        partial.seek(0);
        result = partial.read(pcm);
        check(result.status == ReadStatus::read_error && result.frames_read == 75);
        check(result.native_error == EIO && pcm.back() == -1);
        rejects([&] { partial.read(pcm); });
        partial.seek(10);
        check(partial.read(std::span(pcm).first(cdda_samples_per_frame)).start_lba == 10);
        calls = 0;
        LinuxIoctlReader retry([&](int lba, auto buffer) {
            check(lba == 10);
            if (++calls < 3) return EIO;
            std::fill(buffer.begin(), buffer.end(), 7);
            return 0;
        }, {2});
        retry.seek(10);
        result = retry.read(std::span(pcm).first(cdda_samples_per_frame));
        check(result.status == ReadStatus::ok && result.retries == 2 && calls == 3);
        calls = 0;
        LinuxIoctlReader removed([&](int, auto) { ++calls; return ENOMEDIUM; }, {2});
        removed.seek(0);
        check(removed.read(pcm).status == ReadStatus::read_error && calls == 1);
        calls = 0;
        LinuxIoctlReader exhausted([&](int, auto) { ++calls; return EIO; }, {2});
        exhausted.seek(0);
        result = exhausted.read(pcm);
        check(result.status == ReadStatus::read_error && result.retries == 2 && calls == 3);
        rejects([&] { LinuxIoctlReader invalid([](int, auto) { return 0; }, {0, true}); });
        calls = 0;
        LinuxIoctlReader c2_clean([&](int, auto buffer) {
            ++calls;
            std::fill(buffer.begin(), buffer.end(), 5);
            return 0;
        }, {0, true}, [&](int lba, auto buffer) {
            check(lba == 0 || lba == 75);
            std::fill(buffer.begin(), buffer.end(), 8);
            return C2AudioReadResult{0, lba == 75 ? C2Status::reported : C2Status::clean};
        });
        c2_clean.seek(0);
        result = c2_clean.read(pcm);
        check(result.status == ReadStatus::ok && result.c2_status == C2Status::reported);
        check(calls == 0 && pcm.front() == 8);
        calls = 0;
        LinuxIoctlReader c2_fallback([&](int, auto buffer) {
            ++calls;
            std::fill(buffer.begin(), buffer.end(), 6);
            return 0;
        }, {0, true}, [](int, auto) { return C2AudioReadResult{EIO, C2Status::unknown}; });
        c2_fallback.seek(0);
        result = c2_fallback.read(std::span(pcm).first(cdda_samples_per_frame));
        check(result.status == ReadStatus::ok && result.c2_status == C2Status::unknown);
        check(calls == 1 && pcm.front() == 6);
        LinuxIoctlReader c2_unavailable([](int, auto buffer) {
            std::fill(buffer.begin(), buffer.end(), 4);
            return 0;
        }, {0, false, C2Status::not_available});
        c2_unavailable.seek(0);
        check(c2_unavailable.read(std::span(pcm).first(cdda_samples_per_frame)).c2_status ==
              C2Status::not_available);
        rejects([&] { LinuxIoctlReader invalid_status([](int, auto) { return 0; },
                                                       {0, false, C2Status::clean}); });
        std::array<std::uint8_t, 12> cdb{};
        auto mmc_c2 = make_mmc_c2_audio_read([&](auto command, auto data, auto) {
            check(command.size() == cdb.size());
            std::copy(command.begin(), command.end(), cdb.begin());
            check(data.size() == 2 * (2352 + 294));
            std::fill(data.begin(), data.end(), 0);
            data[2352 + 294 + 2352 + 17] = 1;
            std::fill_n(reinterpret_cast<std::int16_t*>(data.data()), cdda_samples_per_frame, 42);
            return 0;
        });
        auto c2_result = mmc_c2(1234, std::span(pcm).first(2 * cdda_samples_per_frame));
        check(c2_result.error == 0 && c2_result.c2_status == C2Status::reported);
        check(cdb[0] == 0xbe && cdb[1] == 0x04 && cdb[2] == 0 && cdb[3] == 0 &&
              cdb[4] == 0x04 && cdb[5] == 0xd2 && cdb[6] == 0 && cdb[7] == 0 &&
              cdb[8] == 2 && cdb[9] == 0x12 && pcm.front() == 42);
        auto packet_error = make_mmc_c2_audio_read([](auto, auto, auto) { return EPERM; });
        check(packet_error(0, std::span(pcm).first(cdda_samples_per_frame)).error == EPERM);
        std::cout << "PASS: selection, cursor, chunks, partial failure, bounded retry\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
