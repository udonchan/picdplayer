#include "read_offset.hpp"
#include <cerrno>
#include <limits>
#include <vector>

namespace {
class OffsetReader final : public CddaReader {
public:
    OffsetReader(std::unique_ptr<CddaReader> reader, ReadOffsetWindow window)
        : reader_(std::move(reader)), window_(window) {
        if (!reader_ || !window_.playable()) throw std::invalid_argument("no playable offset range");
    }
    void seek(std::int32_t lba) override {
        positioned_ = false;
        if (lba < window_.playable_begin_lba || lba >= window_.playable_end_lba)
            throw std::invalid_argument("seek outside corrected PCM window");
        position_ = lba;
        positioned_ = true;
    }
    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("offset reader requires seek");
        if (pcm.empty() || pcm.size() % cdda_samples_per_frame)
            throw std::invalid_argument("offset PCM must contain whole CD frames");
        const auto frames = pcm.size() / cdda_samples_per_frame;
        ReadResult result{position_, frames, 0, ReadStatus::read_error, ERANGE, 0};
        result.offset = OffsetReadEvidence{window_.offset_samples, std::nullopt};
        positioned_ = false;
        if (frames > static_cast<std::size_t>(window_.playable_end_lba - position_)) return result;
        const auto first_sample = std::int64_t(position_) * 588 + window_.offset_samples;
        const auto last_sample = first_sample + static_cast<std::int64_t>(frames) * 588;
        const auto first_lba = static_cast<std::int32_t>(first_sample / 588);
        const auto end_lba = (last_sample + 587) / 588;
        const auto physical_frames = static_cast<std::size_t>(end_lba - first_lba);
        if (physical_frames > std::numeric_limits<std::size_t>::max() / cdda_samples_per_frame)
            throw std::invalid_argument("offset buffer overflow");
        std::vector<std::int16_t> physical(physical_frames * cdda_samples_per_frame);
        reader_->seek(first_lba);
        const auto raw = reader_->read(physical);
        result.offset->single_physical_read = PhysicalReadEvidence{
            first_lba, physical_frames, raw.start_lba, raw.frames_read, raw.native_error};
        result.native_error = raw.native_error;
        result.retries = raw.retries;
        result.paranoia = raw.paranoia;
        result.read_independence = raw.read_independence;
        result.c2_status = raw.c2_status;
        if (raw.status != ReadStatus::ok || raw.start_lba != first_lba ||
            raw.frames_requested != physical_frames || raw.frames_read != physical_frames) {
            if (!result.native_error) result.native_error = EIO;
            return result;
        }
        const auto skip = static_cast<std::size_t>(first_sample - std::int64_t(first_lba) * 588) * 2;
        std::copy_n(physical.begin() + static_cast<std::ptrdiff_t>(skip), pcm.size(), pcm.begin());
        result.status = ReadStatus::ok;
        result.native_error = 0;
        result.frames_read = frames;
        position_ += static_cast<std::int32_t>(frames);
        positioned_ = true;
        return result;
    }
private:
    std::unique_ptr<CddaReader> reader_;
    ReadOffsetWindow window_;
    std::int32_t position_ = 0;
    bool positioned_ = false;
};
}

std::unique_ptr<CddaReader> make_offset_reader(std::unique_ptr<CddaReader> reader,
                                              ReadOffsetWindow window) {
    return std::make_unique<OffsetReader>(std::move(reader), window);
}
