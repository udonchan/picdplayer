#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

class AudioUnderrun : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual void reset() = 0;
    // Nonblocking write; returns accepted stereo sample frames (not CD frames).
    virtual std::size_t write(std::span<const std::int16_t> samples) = 0;
    virtual std::int64_t delay() = 0;
    virtual bool drain() = 0;
};
std::unique_ptr<AudioOutput> make_alsa_output(const std::string& device,
                                               unsigned latency_microseconds = 200'000);
