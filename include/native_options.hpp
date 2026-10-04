#pragma once

#include <cstdint>

#include "cheats.hpp"
#include "recomp.h"

namespace twine::native_options {

constexpr uint32_t pause_options_definition = 0x800BDB30U;
constexpr uint32_t frontend_options_definition = 0x800BD8D8U;
constexpr uint32_t controller_style_entry = 0x800B62A4U;
constexpr uint32_t advanced_controls_entry = 0x800B62CCU;
constexpr uint32_t graphics_entry = 0x800B62F4U;
constexpr uint32_t frontend_controller_style_entry = 0x8011ACE0U;
constexpr uint32_t frontend_advanced_controls_entry = 0x8011AD08U;
constexpr uint32_t frontend_graphics_entry = 0x8011AD30U;

constexpr bool is_options_definition(uint32_t definition) {
    return definition == pause_options_definition ||
        definition == frontend_options_definition;
}

constexpr uint32_t entry_for(
    uint32_t definition,
    uint32_t pause_entry,
    uint32_t frontend_entry
) {
    return definition == frontend_options_definition
        ? frontend_entry : pause_entry;
}

enum class Tab : uint8_t {
    none,
    controls,
    graphics,
    cheats,
};

constexpr Tab route(uint32_t definition, uint32_t entry) {
    if (twine::cheats::is_main_menu_route(definition, entry)) {
        return Tab::cheats;
    }
    if (!is_options_definition(definition)) {
        return Tab::none;
    }
    if (entry == entry_for(
            definition,
            controller_style_entry,
            frontend_controller_style_entry) ||
            entry == entry_for(
                definition,
                advanced_controls_entry,
                frontend_advanced_controls_entry)) {
        return Tab::controls;
    }
    return entry == entry_for(
        definition,
        graphics_entry,
        frontend_graphics_entry) ? Tab::graphics : Tab::none;
}

void service_ui_request();
Tab take_serviced_tab();

}

extern "C" uint32_t twine_route_native_options(
    uint8_t* rdram,
    recomp_context* ctx);
extern "C" uint32_t twine_cancel_native_options_transition();
