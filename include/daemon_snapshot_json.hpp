#pragma once
#include "daemon_snapshot.hpp"
#include <string>

// Stable JSON representation used by the future GET /api/state endpoint.
std::string serialize_daemon_snapshot(const DaemonSnapshot& snapshot);
