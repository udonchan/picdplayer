#include "linux_ioctl_reader.hpp"
#include "mmc_read_transport.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/stat.h>
#include <unistd.h>
void check(bool ok) { if (!ok) throw std::runtime_error("CDDA check failed"); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid operation accepted");
}
namespace {
unsigned scenario = 0, packet_calls = 0, audio_calls = 0;
int packet_fd = -1;
unsigned expected_timeout = 5000;
}
// This executable never forwards ioctl to real hardware.
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
    struct stat info{};
    check(fstat(fd, &info) == 0 && S_ISREG(info.st_mode));
    check((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
    va_list args;
    va_start(args, request);
    if (request == SG_IO) {
        auto* io = va_arg(args, sg_io_hdr_t*);
        va_end(args);
        check(io->interface_id == 'S' && io->dxfer_direction == SG_DXFER_FROM_DEV);
        check(io->cmd_len == 12 && io->cmdp[0] == 0xbe && io->cmdp[1] == 4 && io->cmdp[8] == 1);
        check(io->cmdp[9] == 0x12 && io->cmdp[10] == 0 && io->cmdp[11] == 0);
        check(io->dxfer_len == 2646 && io->timeout == expected_timeout && io->mx_sb_len > 0);
        auto* data = static_cast<unsigned char*>(io->dxferp);
        std::fill_n(data, io->dxfer_len, 0);
        data[0] = 0x34; data[1] = 0x92;
        packet_fd = fd;
        ++packet_calls;
        if (scenario == 1) data[2352] = 1;
        if (scenario == 2) io->resid = 1;
        if (scenario == 3) io->status = 2;
        if (scenario == 4) io->resid = -1;
        if (scenario == 5) io->resid = 2647;
        if (scenario == 6) { errno = EACCES; return -1; }
        if (scenario == 7) io->driver_status = 8;
        if (scenario == 8) io->host_status = 1;
        if (scenario == 9) io->info = SG_INFO_CHECK;
        return 0;
    }
    if (request == CDROMREADAUDIO) {
        auto* io = va_arg(args, cdrom_read_audio*);
        va_end(args);
        ++audio_calls;
        check(io->nframes == 1);
        std::fill_n(reinterpret_cast<std::int16_t*>(io->buf), cdda_samples_per_frame, 13);
        return 0;
    }
    va_end(args);
    errno = ENOTTY;
    return -1;
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
        auto mmc_c2 = make_mmc_c2_audio_read([&](auto command, auto data, auto, std::size_t& transferred) {
            check(command.size() == cdb.size());
            std::copy(command.begin(), command.end(), cdb.begin());
            check(data.size() == 2 * (2352 + 294));
            std::fill(data.begin(), data.end(), 0);
            data[2352 + 294 + 2352 + 17] = 1;
            for (std::size_t sample = 0; sample < cdda_samples_per_frame; ++sample) data[2 * sample] = 42;
            transferred = data.size();
            return 0;
        });
        auto c2_result = mmc_c2(1234, std::span(pcm).first(2 * cdda_samples_per_frame));
        check(c2_result.error == 0 && c2_result.c2_status == C2Status::reported);
        check(cdb[0] == 0xbe && cdb[1] == 0x04 && cdb[2] == 0 && cdb[3] == 0 &&
              cdb[4] == 0x04 && cdb[5] == 0xd2 && cdb[6] == 0 && cdb[7] == 0 &&
              cdb[8] == 2 && cdb[9] == 0x12 && pcm.front() == 42);
        auto packet_error = make_mmc_c2_audio_read([](auto, auto, auto, std::size_t&) { return EPERM; });
        check(packet_error(0, std::span(pcm).first(cdda_samples_per_frame)).error == EPERM);
        for (const std::size_t count : {std::size_t{0}, std::size_t{2352}, std::size_t{2645}, std::size_t{2647}}) {
            auto short_packet = make_mmc_c2_audio_read([count](auto, auto data, auto, std::size_t& received) {
                std::fill(data.begin(), data.end(), 0);
                received = count;
                return 0;
            });
            std::fill(pcm.begin(), pcm.end(), -1234);
            const auto invalid = short_packet(0, std::span(pcm).first(cdda_samples_per_frame));
            check(invalid.error == EMSGSIZE && invalid.c2_status == C2Status::unknown && pcm.front() == -1234);
            LinuxIoctlReader fallback([](int, auto block) { std::fill(block.begin(), block.end(), 13); return 0; },
                                     {0, true}, short_packet);
            fallback.seek(0);
            const auto read = fallback.read(std::span(pcm).first(cdda_samples_per_frame));
            check(read.status == ReadStatus::ok && read.c2_status == C2Status::unknown && pcm.front() == 13);
        }
        unsigned packets = 0;
        auto full_packet = make_mmc_c2_audio_read([&](auto, auto data, auto, std::size_t& received) {
            ++packets;
            std::fill(data.begin(), data.end(), 0);
            data[0] = 0x34; data[1] = 0x92; // Explicit little-endian negative sample.
            received = data.size();
            return 0;
        });
        check(full_packet(0, std::span(pcm).first(cdda_samples_per_frame)).c2_status == C2Status::clean);
        check(pcm.front() == std::bit_cast<std::int16_t>(std::uint16_t{0x9234}));
        const auto before = packets;
        check(full_packet(0, pcm).error == EINVAL && packets == before); // 76 sectors exceeds the per-command bound.
        const auto node = std::filesystem::temp_directory_path() /
            ("picdplayer-c2-packet-" + std::to_string(::getpid()));
        { std::ofstream out(node); check(static_cast<bool>(out)); }
        for (scenario = 0; scenario < 10; ++scenario) {
            const auto previous_audio = audio_calls;
            auto real = make_cdda_reader(CddaBackend::direct, node.string(), {0, true});
            real->seek(0);
            const auto observed = real->read(std::span(pcm).first(cdda_samples_per_frame));
            check(observed.status == ReadStatus::ok && observed.frames_read == 1);
            check(observed.c2_status == (scenario == 0 ? C2Status::clean : scenario == 1 ? C2Status::reported : C2Status::unknown));
            check(audio_calls == previous_audio + (scenario >= 2 ? 1 : 0));
            check(pcm.front() == (scenario >= 2 ? 13 : std::bit_cast<std::int16_t>(std::uint16_t{0x9234})));
            check(fcntl(packet_fd, F_GETFD) >= 0);
            real.reset();
            check(fcntl(packet_fd, F_GETFD) == -1 && errno == EBADF);
        }
        check(packet_calls == 10);
        const int fd = open(node.c_str(), O_RDONLY | O_CLOEXEC);
        check(fd >= 0);
        std::array<std::uint8_t, 12> request{};
        request[0] = 0xbe; request[1] = 4; request[8] = 1; request[9] = 0x12;
        std::array<std::uint8_t, 2646> data{};
        std::array<std::uint8_t, sizeof(request_sense)> sense{};
        const auto issued = packet_calls;
        for (const unsigned field : {0U, 1U, 8U, 9U, 10U, 11U}) {
            auto invalid = request;
            invalid[field] = field == 10 || field == 11 ? 1 : 0;
            std::size_t transferred = 77;
            check(picdplayer_detail::read_mmc_packet(fd, invalid, data, sense, transferred, 5000) == EINVAL);
            check(transferred == 0 && packet_calls == issued);
        }
        request[8] = 76;
        std::size_t transferred = 77;
        std::vector<std::uint8_t> oversized(76 * 2646);
        check(picdplayer_detail::read_mmc_packet(fd, request, oversized, sense, transferred, 5000) == EINVAL);
        request[8] = 1;
        check(picdplayer_detail::read_mmc_packet(fd, request, std::span(data).first(2645), sense, transferred, 5000) == EINVAL);
        check(packet_calls == issued);
        close(fd);
        DirectOptions strict{0, true, C2Status::not_checked, true, 1000};
        expected_timeout = 1000;
        scenario = 2; // A successful syscall with one unreturned byte must not use normal audio.
        const auto audio_before = audio_calls;
        auto diagnostic = make_cdda_reader(CddaBackend::direct, node.string(), strict);
        diagnostic->seek(0);
        std::fill(pcm.begin(), pcm.end(), -1234);
        const auto unavailable = diagnostic->read(std::span(pcm).first(cdda_samples_per_frame));
        check(unavailable.status == ReadStatus::read_error && unavailable.frames_read == 0);
        check(unavailable.native_error == EMSGSIZE && unavailable.c2_status == C2Status::unknown);
        check(audio_calls == audio_before && pcm.front() == -1234);
        diagnostic.reset();
        check(fcntl(packet_fd, F_GETFD) == -1 && errno == EBADF);
        scenario = 0;
        diagnostic = make_cdda_reader(CddaBackend::direct, node.string(), strict);
        diagnostic->seek(0);
        check(diagnostic->read(std::span(pcm).first(cdda_samples_per_frame)).c2_status == C2Status::clean);
        diagnostic.reset();
        check(fcntl(packet_fd, F_GETFD) == -1 && errno == EBADF);
        expected_timeout = 5000;
        rejects([&] { LinuxIoctlReader invalid([](int, auto) { return 0; }, {0, false, C2Status::not_checked, true}); });
        rejects([&] { make_cdda_reader(CddaBackend::direct, node.string(), {0, true, C2Status::not_checked, true, 0}); });
        rejects([&] { make_cdda_reader(CddaBackend::direct, node.string(), {0, true, C2Status::not_checked, true, 5001}); });
        std::filesystem::remove(node);
        std::cout << "PASS: selection, cursor, chunks, partial failure, bounded retry\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
