#pragma once

#include "integrity_state.hpp"
#include <cstdint>

enum class PlayerEventType { read_observed };
enum class EventSeverity { debug, info, warning, error };
enum class PresentationPriority { background, normal, activity, important, sticky };

struct PlayerEvent {
    std::uint64_t sequence = 0;
    std::uint64_t stream_generation = 0;
    PlayerEventType type = PlayerEventType::read_observed;
    EventSeverity severity = EventSeverity::debug;
    PresentationPriority presentation = PresentationPriority::background;
    ReadEvidence read;
};

const char* player_event_type_name(PlayerEventType value);
const char* event_severity_name(EventSeverity value);
const char* presentation_priority_name(PresentationPriority value);
