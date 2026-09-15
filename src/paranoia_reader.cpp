#include "cdda_reader.hpp"
#define DO_NOT_WANT_PARANOIA_COMPATIBILITY
#include <cdio/paranoia/paranoia.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
using Drive = std::unique_ptr<cdrom_drive_t, decltype(&cdio_cddap_close)>;
using Paranoia = std::unique_ptr<cdrom_paranoia_t, decltype(&cdio_paranoia_free)>;
thread_local ParanoiaEvents* active_events = nullptr;
void callback(long, paranoia_cb_mode_t mode) noexcept {
    if (!active_events) return;
    switch (mode) {
    case PARANOIA_CB_READ: ++active_events->reads; break;
    case PARANOIA_CB_VERIFY: ++active_events->verifies; break;
    case PARANOIA_CB_FIXUP_EDGE: case PARANOIA_CB_FIXUP_ATOM:
    case PARANOIA_CB_FIXUP_DROPPED: case PARANOIA_CB_FIXUP_DUPED:
        ++active_events->fixups; break;
    case PARANOIA_CB_SKIP: ++active_events->skips; break;
    case PARANOIA_CB_READERR: ++active_events->read_errors; break;
    case PARANOIA_CB_CACHEERR: ++active_events->cache_errors; break;
    default: ++active_events->other; break;
    }
}
struct CallbackScope {
    ParanoiaEvents* previous;
    explicit CallbackScope(ParanoiaEvents& events) : previous(active_events) { active_events = &events; }
    ~CallbackScope() { active_events = previous; }
};
class ParanoiaReader final : public CddaReader {
    Drive drive_{nullptr, cdio_cddap_close};
    Paranoia paranoia_{nullptr, cdio_paranoia_free};
    std::int32_t cursor_ = 0;
    bool positioned_ = false;
public:
    explicit ParanoiaReader(const std::string& path) {
        drive_.reset(cdio_cddap_identify(path.c_str(), CDDA_MESSAGE_FORGETIT, nullptr));
        if (!drive_) throw std::runtime_error("paranoia: cannot identify " + path);
        const int error = cdio_cddap_open(drive_.get());
        if (error != 0) throw std::runtime_error("paranoia: open failed code=" + std::to_string(error));
        drive_->b_swap_bytes = true;
        paranoia_.reset(cdio_paranoia_init(drive_.get()));
        if (!paranoia_) throw std::runtime_error("paranoia: initialization failed");
        cdio_paranoia_modeset(paranoia_.get(), PARANOIA_MODE_FULL & ~PARANOIA_MODE_NEVERSKIP);
    }
    void seek(std::int32_t lba) override {
        if (lba < 0) throw std::invalid_argument("negative CDDA LBA");
        positioned_ = false;
        // Returns the previous cursor, not the requested target (10.2+2.0.2).
        if (cdio_paranoia_seek(paranoia_.get(), lba, SEEK_SET) == -1)
            throw std::runtime_error("paranoia: seek failed lba=" + std::to_string(lba));
        cursor_ = lba;
        positioned_ = true;
    }
    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("CDDA seek required before read");
        if (pcm.empty() || pcm.size() % cdda_samples_per_frame)
            throw std::invalid_argument("PCM buffer must contain whole CD frames");
        const auto count = pcm.size() / cdda_samples_per_frame;
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - cursor_))
            throw std::invalid_argument("CDDA LBA overflow");
        ReadResult result{cursor_, count, 0, ReadStatus::ok, 0, 0};
        CallbackScope scope(result.paranoia);
        for (std::size_t i = 0; i < count; ++i) {
            errno = 0;
            const auto samples = cdio_paranoia_read_limited(paranoia_.get(), callback, 20);
            const int error = errno;
            if (!samples || result.paranoia.skips) {
                result.status = ReadStatus::read_error;
                result.native_error = error; // zero means no errno was supplied, not success.
                positioned_ = false;
                return result;
            }
            std::copy_n(samples, cdda_samples_per_frame, pcm.begin() + i * cdda_samples_per_frame);
            ++cursor_;
            ++result.frames_read;
        }
        return result;
    }
};
}
std::unique_ptr<CddaReader> make_paranoia_reader(const std::string& device) {
    return std::make_unique<ParanoiaReader>(device);
}
