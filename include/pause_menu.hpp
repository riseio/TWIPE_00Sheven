#ifndef TWINE_PAUSE_MENU_HPP
#define TWINE_PAUSE_MENU_HPP

#include <cstdint>

namespace twine::pause_menu {

struct Context {
    bool session = false;
    bool menu_present = false;
    bool root = false;
    unsigned selection = 0;
};

constexpr bool shortcut_input_available(
    bool gameplay_input_disabled,
    const Context& context
) {

    return !gameplay_input_disabled || context.menu_present;
}

constexpr bool quit_confirmation_owned_by_modern_controls(
    bool menu_present,
    int16_t page,
    uint8_t demo_mode,
    uint32_t manager_table,
    uint32_t manager_definition,
    uint16_t manager_mode,
    uint32_t pause_menu_table,
    uint32_t frontend_menu_table,
    uint32_t debrief_menu_definition
) {

    if (!menu_present || page != 24 || demo_mode != 0) {
        return false;
    }
    return (manager_table == pause_menu_table &&
            manager_definition == pause_menu_table && manager_mode == 1U) ||
        (manager_table == frontend_menu_table &&
            manager_definition == debrief_menu_definition &&
            manager_mode == 16U);
}

}

#endif
