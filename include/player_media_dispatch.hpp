#pragma once

#include "media_worker.hpp"
#include <chrono>
#include <utility>

// Called after delivered media results and engine updates have been applied.
// Service ready commands first; neither callback nor a start request is retained.
template<class Clock = std::chrono::steady_clock, class ServiceCommands, class CanPoll>
void service_commands_before_media_poll(MediaWorker& media, typename Clock::time_point& next_media,
                                       ServiceCommands&& service, CanPoll&& can_poll) {
    std::forward<ServiceCommands>(service)();
    const auto now = Clock::now();
    if (now < next_media) return;
    if (std::forward<CanPoll>(can_poll)()) (void)media.request(MediaWork::observe);
    next_media = now + std::chrono::milliseconds(500);
}
