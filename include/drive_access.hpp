#pragma once

#include <mutex>
#include <utility>

// Serializes kernel/library calls that address the same optical drive. It does
// not interrupt an ioctl already in progress; lifecycle code still cancels the
// PCM stream and waits for reader release before ejecting.
class DriveAccessCoordinator {
public:
    template<class Function>
    decltype(auto) invoke(Function&& function) {
        std::lock_guard lock(mutex_);
        return std::forward<Function>(function)();
    }
private:
    std::mutex mutex_;
};
