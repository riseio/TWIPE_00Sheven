#include "resume_recovery.hpp"

#include <chrono>

#ifdef __linux__
#include <ctime>
#endif

namespace {

constexpr uint64_t suspend_gap_nanoseconds = 2'000'000'000ULL;
constexpr uint64_t fallback_gap_nanoseconds = 3'000'000'000ULL;

uint64_t active_nanoseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool boot_nanoseconds(uint64_t& value) {
#if defined(__linux__) && defined(CLOCK_BOOTTIME)
    timespec boot_time{};
    if (clock_gettime(CLOCK_BOOTTIME, &boot_time) == 0 &&
            boot_time.tv_sec >= 0 && boot_time.tv_nsec >= 0) {
        value = static_cast<uint64_t>(boot_time.tv_sec) * 1'000'000'000ULL +
            static_cast<uint64_t>(boot_time.tv_nsec);
        return true;
    }
#endif
    value = 0;
    return false;
}

}

bool twine::resume_recovery::ClockTracker::observe(
    uint64_t active_nanoseconds,
    uint64_t boot_nanoseconds,
    bool boot_clock_available
) {
    if (!initialized || active_nanoseconds < previous_active_nanoseconds ||
            (boot_clock_available &&
                boot_nanoseconds < previous_boot_nanoseconds)) {
        previous_active_nanoseconds = active_nanoseconds;
        previous_boot_nanoseconds = boot_nanoseconds;
        initialized = true;
        return false;
    }

    const uint64_t active_delta =
        active_nanoseconds - previous_active_nanoseconds;
    const uint64_t boot_delta = boot_clock_available
        ? boot_nanoseconds - previous_boot_nanoseconds : 0;
    previous_active_nanoseconds = active_nanoseconds;
    previous_boot_nanoseconds = boot_nanoseconds;

    if (!boot_clock_available) {
        return active_delta >= fallback_gap_nanoseconds;
    }

    return boot_delta > active_delta &&
        boot_delta - active_delta >= suspend_gap_nanoseconds;
}

bool twine::resume_recovery::detect_clock_gap() {
    static ClockTracker tracker;
    uint64_t boot_time = 0;
    const bool boot_clock_available = boot_nanoseconds(boot_time);
    return tracker.observe(
        active_nanoseconds(),
        boot_time,
        boot_clock_available);
}
