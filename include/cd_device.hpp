#pragma once
#include "disc_toc.hpp"
#include "media_state.hpp"
#include <optional>
#include <string>

struct CdMediaSnapshot {
    MediaObservation observation;
    int drive_status;
    std::optional<int> disc_status;
};

// One-shot read-only status. Opens and closes the device within the call.
CdMediaSnapshot read_cd_media(const std::string& device);

// One-shot drive/disc classification diagnostic. Does not change tray state.
// No application-level TOC extraction; the kernel may inspect TOC to classify.
void probe_cd_media(const std::string& device);

// Read-only TOC diagnostic for audio-only CDs.
void probe_cd_toc(const std::string& device);

// Returns a validated model; owns no device handle after return.
DiscToc read_cd_toc(const std::string& device);

// Opens the device only for the duration of CDROMEJECT.
void eject_cd(const std::string& device);
