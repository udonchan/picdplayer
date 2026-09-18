#pragma once
#include "daemon_snapshot.hpp"
#include <string>
#include <string_view>

// Stable JSON representation used by the future GET /api/state endpoint.
std::string serialize_daemon_snapshot(const DaemonSnapshot& snapshot);

// Snapshot JSON differs only in this transport revision when no projected
// daemon state changed.  This avoids constructing the same large JSON twice.
bool snapshot_json_equal_ignoring_revision(std::string_view left, std::string_view right);
