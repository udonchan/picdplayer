#include "audio_output.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
int main() {
    try {
        // ALSA null plugin: exercises native API without a sound device.
        auto output = make_alsa_output("null", 500'000);
        output->reset();
        std::vector<std::int16_t> samples(8820, 0);
        std::size_t offset = 0;
        for (int i = 0; i < 100 && offset < samples.size(); ++i)
            offset += output->write(std::span<const std::int16_t>(samples).subspan(offset)) * 2;
        if (offset != samples.size() || output->delay() < 0) throw std::runtime_error("ALSA null write failed");
        if (!output->drain()) throw std::runtime_error("ALSA null did not drain");
        output->reset();
        std::cout << "PASS: ALSA null configure/write/drain/reset\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
