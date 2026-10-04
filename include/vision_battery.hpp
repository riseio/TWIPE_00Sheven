#pragma once

#include <cstdint>

namespace twine::vision_battery {

enum class Mode : uint8_t { Off, NightVision, Xray };

struct Status {
    Mode mode = Mode::Off;
    uint8_t percent = 0;
    bool operator==(const Status&) const = default;
};

inline constexpr unsigned full_charge = 0x706;

Status read_status(uint8_t* rdram, bool gameplay);
void publish(uint8_t* rdram, bool gameplay);
void update_ui();

}
