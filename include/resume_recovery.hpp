#pragma once

#include <cstdint>

namespace twine::resume_recovery {

class ClockTracker {
public:
    bool observe(
        uint64_t active_nanoseconds,
        uint64_t boot_nanoseconds,
        bool boot_clock_available);

private:
    uint64_t previous_active_nanoseconds = 0;
    uint64_t previous_boot_nanoseconds = 0;
    bool initialized = false;
};

bool detect_clock_gap();

}
