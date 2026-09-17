#include "drive_access.hpp"
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace { void check(bool value) { if (!value) throw std::runtime_error("drive access test failed"); } }

int main() {
    try {
        DriveAccessCoordinator coordinator;
        check(coordinator.invoke([] { return 42; }) == 42);

        std::mutex state_mutex;
        std::condition_variable changed;
        bool first_entered = false, release_first = false;
        bool second_attempting = false, second_entered = false;
        std::thread first([&] {
            coordinator.invoke([&] {
                std::unique_lock lock(state_mutex);
                first_entered = true; changed.notify_all();
                changed.wait(lock, [&] { return release_first; });
            });
        });
        {
            std::unique_lock lock(state_mutex);
            changed.wait(lock, [&] { return first_entered; });
        }
        std::thread second([&] {
            {
                std::lock_guard lock(state_mutex);
                second_attempting = true; changed.notify_all();
            }
            coordinator.invoke([&] {
                std::lock_guard lock(state_mutex);
                second_entered = true; changed.notify_all();
            });
        });
        {
            std::unique_lock lock(state_mutex);
            changed.wait(lock, [&] { return second_attempting; });
            check(!second_entered);
            release_first = true; changed.notify_all();
            changed.wait(lock, [&] { return second_entered; });
        }
        first.join(); second.join();
        std::cout << "PASS: optical drive operations are serialized\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
