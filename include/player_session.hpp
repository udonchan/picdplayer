#pragma once
#include "cdda_reader.hpp"
void run_player_session(const std::string& device, CddaBackend backend,
                        const std::string& audio_device, bool cec, const std::string& cec_device,
                        bool cec_diagnostics);
