#pragma once
#include "cdda_reader.hpp"
#include "pcm_worker.hpp"
#include "read_policy.hpp"
void run_player_session(const std::string& device, CddaBackend backend,
                        const std::string& audio_device, unsigned audio_latency_ms,
                        bool cec, const std::string& cec_device,
                        bool cec_diagnostics, bool interactive, bool metadata_enabled,
                        const std::string& metadata_cache, const std::string& api_listen,
                        int api_port, PcmBufferConfig buffer_config = {},
                        ReadPolicy read_policy = {}, const std::string& custom_ui = {});
