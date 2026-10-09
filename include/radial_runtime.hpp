#ifndef RADIAL_RUNTIME_HPP
#define RADIAL_RUNTIME_HPP

#include <cstdint>
#include "recomp.h"

namespace twine::radial {

struct InputResult {
    bool capture = false;
    bool consume_xray_binding = false;
    uint16_t consumed_buttons = 0;
    uint8_t tap_actions = 0;
};

struct EquipmentContext {
    uint8_t item = 0xFF;
    bool scoped = false;
    bool zoom_available = false;
};

void initialize_ui();
void update_ui();
InputResult update_input(
    int player,
    bool weapon_down,
    bool gadget_down,
    bool xray_toggle_down,
    uint8_t watch_shortcuts,
    bool cancel_down,
    float direction_x,
    float direction_y,
    int32_t weapon_scroll = 0);
void clear(int player);
bool active(int player);
uint64_t weapon_mask(int player);
EquipmentContext equipment_context(int player);
uint8_t current_item(int player);
uint8_t selected_gadget(int player);
bool scoped(int player);
bool direction_for_weapon(int player, uint8_t item, float& x, float& y);
bool direction_for_gadget(int player, uint8_t item, float& x, float& y);

}

extern "C" void twine_radial_sync(uint8_t* rdram, recomp_context* ctx);
extern "C" void twine_radial_apply_weapon(uint8_t* rdram, recomp_context* ctx);
extern "C" void twine_radial_prepare_gadget(uint8_t* rdram, recomp_context* ctx);
extern "C" void twine_radial_apply_special(uint8_t* rdram, recomp_context* ctx);
extern "C" uint32_t twine_radial_should_freeze(uint8_t* rdram, recomp_context* ctx);

#endif
