#include "player_event.hpp"

const char* player_event_type_name(PlayerEventType value) {
    switch (value) { case PlayerEventType::read_observed: return "READ_OBSERVED"; }
    return "READ_OBSERVED";
}
const char* event_severity_name(EventSeverity value) {
    switch (value) {
    case EventSeverity::debug: return "DEBUG";
    case EventSeverity::info: return "INFO";
    case EventSeverity::warning: return "WARNING";
    case EventSeverity::error: return "ERROR";
    }
    return "INFO";
}
const char* presentation_priority_name(PresentationPriority value) {
    switch (value) {
    case PresentationPriority::background: return "BACKGROUND";
    case PresentationPriority::normal: return "NORMAL";
    case PresentationPriority::activity: return "ACTIVITY";
    case PresentationPriority::important: return "IMPORTANT";
    case PresentationPriority::sticky: return "STICKY";
    }
    return "BACKGROUND";
}
