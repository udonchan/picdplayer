#include "cdda_reader.hpp"
#define DO_NOT_WANT_PARANOIA_COMPATIBILITY
#include <cdio/paranoia/paranoia.h>
#include <array>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
cdrom_drive_t drive{};
int token, reads, closes, frees, mode, fail_kind;
std::int32_t position;
std::array<std::int16_t, 1176> samples;
void check(bool ok) { if (!ok) throw std::runtime_error("paranoia test failed"); }
template<class F> void rejects(F f) {
    try { f(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid operation accepted");
}
}
extern "C" {
cdrom_drive_t* __wrap_cdio_cddap_identify(const char*, int, char**) { return &drive; }
int __wrap_cdio_cddap_open(cdrom_drive_t*) { return fail_kind == 3 ? -1 : 0; }
int __wrap_cdio_cddap_close(cdrom_drive_t*) { ++closes; return 1; }
cdrom_paranoia_t* __wrap_cdio_paranoia_init(cdrom_drive_t*) { return reinterpret_cast<cdrom_paranoia_t*>(&token); }
void __wrap_cdio_paranoia_free(cdrom_paranoia_t*) { ++frees; }
void __wrap_cdio_paranoia_modeset(cdrom_paranoia_t*, int value) { mode = value; }
lsn_t __wrap_cdio_paranoia_seek(cdrom_paranoia_t*, int32_t target, int whence) {
    check(whence == SEEK_SET);
    const auto old = position;
    position = target;
    return old;
}
int16_t* __wrap_cdio_paranoia_read_limited(cdrom_paranoia_t*, void(*cb)(long, paranoia_cb_mode_t), int max_retries) {
    check(max_retries == 20);
    ++reads;
    cb(position * 1176, PARANOIA_CB_READ);
    if (fail_kind == 1) cb(position * 1176, PARANOIA_CB_SKIP);
    if (fail_kind == 2) return nullptr;
    samples.fill(static_cast<std::int16_t>(position++));
    return samples.data();
}
}
int main() {
    try {
        require_cdda_backend(CddaBackend::paranoia);
        {
            auto reader = make_cdda_reader(CddaBackend::paranoia, "fake");
            check(mode == (PARANOIA_MODE_FULL & ~PARANOIA_MODE_NEVERSKIP));
            check(drive.b_swap_bytes);
            std::vector<std::int16_t> pcm(2 * 1176, -1);
            rejects([&] { reader->read(pcm); });
            reader->seek(150);
            auto r = reader->read(pcm);
            check(r.frames_read == 2 && r.start_lba == 150 && r.paranoia.reads == 2);
            check(pcm[0] == 150 && pcm[1176] == 151); // Must copy before next library read.
            check(reads == 2);
            check(reader->read(pcm).start_lba == 152);
            fail_kind = 1;
            pcm.assign(pcm.size(), -1);
            r = reader->read(pcm);
            check(r.status == ReadStatus::read_error && r.frames_read == 0 && r.paranoia.skips == 1);
            check(pcm[0] == -1);
            rejects([&] { reader->read(pcm); });
            reader->seek(100);
            fail_kind = 2;
            check(reader->read(pcm).status == ReadStatus::read_error);
            fail_kind = 0;
            reader->seek(200);
            check(reader->read(pcm).start_lba == 200);
            rejects([&] { reader->seek(-1); });
        }
        check(closes == 1 && frees == 1);
        fail_kind = 3;
        rejects([] { make_cdda_reader(CddaBackend::paranoia, "fake"); });
        check(closes == 2 && frees == 1);
        std::cout << "PASS: paranoia ownership, sequential reads, seek, copy, skip and failure\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
