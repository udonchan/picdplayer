#pragma once

#include "complete_pcm_input.hpp"

// Local fingerprints of canonical input, not comparison against a reference.
const CompletePcmProfile& complete_pcm_sha256_profile() noexcept;
std::unique_ptr<CompletePcmConsumer> make_complete_pcm_sha256_consumer();
