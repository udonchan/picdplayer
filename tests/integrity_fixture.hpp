#pragma once

#include "cdda_reader.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

// Test-only reader for deterministic Integrity observations. Each step states
// the logical LBA and requested/read frame counts; it never models a drive,
// C1/C2 evidence, physical rereads, or transfer speed.
struct ScriptedRead {
    ReadResult result;
    std::int16_t sample = 0;
};

class ScriptedCddaReader final : public CddaReader {
public:
    explicit ScriptedCddaReader(std::vector<ScriptedRead> script)
        : script_(std::move(script)) {}

    void seek(std::int32_t lba) override {
        if (next_ == script_.size()) throw std::runtime_error("script exhausted");
        if (script_[next_].result.start_lba != lba)
            throw std::logic_error("unexpected scripted seek");
        position_ = lba;
        positioned_ = true;
    }

    ReadResult read(std::span<std::int16_t> pcm) override {
        if (!positioned_) throw std::logic_error("seek required");
        if (next_ == script_.size()) throw std::runtime_error("script exhausted");
        const auto& step = script_[next_];
        const auto available_frames = pcm.size() / cdda_samples_per_frame;
        if (step.result.start_lba != position_ ||
            step.result.frames_requested != available_frames ||
            step.result.frames_read > step.result.frames_requested ||
            step.result.frames_read > available_frames)
            throw std::logic_error("invalid scripted read");
        std::fill_n(pcm.begin(), static_cast<std::ptrdiff_t>(
            step.result.frames_read * cdda_samples_per_frame), step.sample);
        if (step.result.status == ReadStatus::ok)
            position_ += static_cast<std::int32_t>(step.result.frames_read);
        else
            positioned_ = false;
        ++next_;
        return step.result;
    }

private:
    std::vector<ScriptedRead> script_;
    std::size_t next_ = 0;
    std::int32_t position_ = 0;
    bool positioned_ = false;
};
