#include "audio_output.hpp"
#include <alsa/asoundlib.h>
#include <algorithm>
#include <stdexcept>

namespace {
void checked(int code, const char* action) {
    if (code < 0) throw std::runtime_error(std::string(action) + ": " + snd_strerror(code));
}
class AlsaOutput final : public AudioOutput {
    snd_pcm_t* pcm_ = nullptr;
public:
    explicit AlsaOutput(const std::string& device) {
        checked(snd_pcm_open(&pcm_, device.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK), "ALSA open");
        try {
            checked(snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
                                       2, 44100, 0, 200000), "ALSA configure 44100Hz stereo");
        } catch (...) { snd_pcm_close(pcm_); throw; }
    }
    ~AlsaOutput() override { snd_pcm_drop(pcm_); snd_pcm_close(pcm_); }
    void reset() override {
        checked(snd_pcm_drop(pcm_), "ALSA drop");
        checked(snd_pcm_prepare(pcm_), "ALSA prepare");
    }
    std::size_t write(std::span<const std::int16_t> samples) override {
        if (samples.size() % 2) throw std::invalid_argument("ALSA requires stereo frames");
        const auto result = snd_pcm_writei(pcm_, samples.data(), samples.size() / 2);
        if (result == -EAGAIN || result == -EINTR) return 0;
        checked(static_cast<int>(result), "ALSA write (underrun stops playback)");
        return static_cast<std::size_t>(result);
    }
    std::int64_t delay() override {
        snd_pcm_sframes_t value = 0;
        checked(snd_pcm_delay(pcm_, &value), "ALSA delay");
        return std::max<snd_pcm_sframes_t>(0, value);
    }
    bool drain() override {
        const auto result = snd_pcm_drain(pcm_);
        if (result == -EAGAIN || result == -EINTR) return false;
        checked(result, "ALSA drain"); return true;
    }
};
}
std::unique_ptr<AudioOutput> make_alsa_output(const std::string& device) {
    return std::make_unique<AlsaOutput>(device);
}
