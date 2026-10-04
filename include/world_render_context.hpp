#pragma once

#include <cstdint>
#include <array>
#include "recomp.h"

namespace twine::render {
    struct NativeCamera {
        std::array<float, 3> position{};
        std::array<float, 3> forward{};
        std::array<float, 3> aim{};
        uint32_t room = 0;
    };

    bool read_native_camera(uint8_t* rdram, recomp_context* ctx, uint32_t player, NativeCamera& camera);
    void publish_native_camera(uint8_t* rdram, recomp_context* ctx, uint32_t player);

    constexpr bool mission_world_active(int16_t overlay, uint8_t demo_mode,
            bool mission_pause_present = false) {

        return demo_mode == 0 &&
            (overlay > 1 || (overlay == 1 && mission_pause_present));
    }
}
