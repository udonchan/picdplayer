#pragma once
#include "cdda_reader.hpp"
void probe_cdda(const std::string& device, CddaBackend backend, int track, int frames, unsigned retries, const std::string& output = {});
