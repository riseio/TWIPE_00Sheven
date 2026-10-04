#pragma once

#include <cstddef>
#include <cstdint>

namespace twine::deck_grips {

inline constexpr uint16_t btn_gripl = 0x224;
inline constexpr uint16_t btn_gripr = 0x225;
inline constexpr uint16_t btn_gripl2 = 0x226;
inline constexpr uint16_t btn_gripr2 = 0x227;
inline constexpr uint16_t btn_south = 0x130;
inline constexpr uint16_t btn_east = 0x131;
inline constexpr size_t raw_reader_capacity = 8;

inline bool is_deck_controller(uint16_t vendor, uint16_t product) {
    return vendor == 0x28DE && product == 0x1205;
}

inline int32_t resolve_physical_face_owner(
    bool physical_faces_available,
    int32_t exact_deck_instance,
    int32_t active_controller_instance
) {
    if (!physical_faces_available) {
        return -1;
    }
    return exact_deck_instance >= 0
        ? exact_deck_instance
        : active_controller_instance;
}

inline bool logical_controller_owns_physical_faces(
    bool single_player_mode,
    int32_t logical_controller,
    int32_t physical_controller_instance,
    int32_t assigned_controller_instance
) {
    if (physical_controller_instance < 0) {
        return false;
    }
    if (single_player_mode) {
        return logical_controller == 0;
    }
    return assigned_controller_instance >= 0 &&
        assigned_controller_instance == physical_controller_instance;
}

struct Edges {
    bool a = false;
    bool b = false;
    bool l4 = false;
    bool r4 = false;
    bool l5 = false, r5 = false;
    bool l5_held = false, r5_held = false, lower_available = false;
};

inline Edges parse_key_event(uint16_t type, uint16_t code, int32_t value) {
    constexpr uint16_t ev_key = 0x01;
    if (type != ev_key || value != 1) {
        return {};
    }
    return {
        code == btn_south,
        code == btn_east,
        code == btn_gripl,
        code == btn_gripr,
        code == btn_gripl2,
        code == btn_gripr2,
    };
}

inline bool is_deck_report(const uint8_t* report, size_t size) {
    return size == 64 && report != nullptr && report[0] == 1 &&
        report[1] == 0 && report[2] == 9 && report[3] == 64;
}

inline Edges parse_report(
    const uint8_t* report,
    size_t size,
    bool& a_held,
    bool& b_held,
    bool& l4_held,
    bool& r4_held,
    bool& l5_held,
    bool& r5_held
) {
    if (!is_deck_report(report, size)) {
        return {};
    }
    const bool a = (report[8] & (1U << 7)) != 0;
    const bool b = (report[8] & (1U << 5)) != 0;
    const bool l4 = (report[13] & (1U << 1)) != 0;
    const bool r4 = (report[13] & (1U << 2)) != 0;
    const bool l5 = (report[9] & (1U << 7)) != 0;
    const bool r5 = (report[10] & 1U) != 0;
    const Edges result{
        a && !a_held,
        b && !b_held,
        l4 && !l4_held,
        r4 && !r4_held,
        l5 && !l5_held, r5 && !r5_held, l5, r5, true,
    };
    a_held = a;
    b_held = b;
    l4_held = l4;
    r4_held = r4;
    l5_held = l5; r5_held = r5;
    return result;
}

Edges poll();
bool physical_faces_available();
int32_t controller_instance();
void recover_after_resume();

}
