#include "metadata_probe.hpp"
#include "cd_device.hpp"
#include "musicbrainz_disc_id.hpp"
#include <iostream>

void probe_musicbrainz_disc_id(const std::string& device) {
    const auto toc = read_cd_toc(device);
    const auto result = calculate_musicbrainz_disc_id(toc);
    std::cout << "metadata: disc_id=" << result.id << '\n'
              << "metadata: toc=" << result.toc << '\n';
}
