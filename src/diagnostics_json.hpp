#pragma once
#include "daemon_snapshot.hpp"
#include <nlohmann/json.hpp>

// Shared diagnostic projection; no provider metadata or artwork URLs.
nlohmann::json diagnostic_fields(const DriveCapabilities& drive, const ReadDiagnostics& read,
                                 const std::vector<PlayerEvent>& recent_events);
