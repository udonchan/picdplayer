#pragma once

#include <string>

// One-shot diagnostic: existing Linux ioctl TOC path followed by Disc ID calculation.
void probe_musicbrainz_disc_id(const std::string& device);
