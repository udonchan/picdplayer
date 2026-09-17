#pragma once
#include "cdda_reader.hpp"
#include "pcm_worker.hpp"
void run_player_session(const std::string& device, CddaBackend backend,
                        const std::string& audio_device, bool cec, const std::string& cec_device,
                        bool cec_diagnostics, bool interactive, bool metadata_enabled,
                        const std::string& metadata_cache, const std::string& api_listen,
                        int api_port, PcmBufferConfig buffer_config = {},
                        bool repeated_read_verification = false);
