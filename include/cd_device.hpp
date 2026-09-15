#pragma once
#include <string>
#include "disc_toc.hpp"

// One-shot drive/disc classification diagnostic. Does not change tray state.
// No application-level TOC extraction; the kernel may inspect TOC to classify.
void probe_cd_media(const std::string& device);

// Read-only TOC diagnostic for audio-only CDs.
void probe_cd_toc(const std::string& device);

// Returns a validated model; owns no device handle after return.
DiscToc read_cd_toc(const std::string& device);
