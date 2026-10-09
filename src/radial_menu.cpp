#include "rom_metadata.hpp"
#include "save_state_owner.hpp"
#include "radial_runtime.hpp"
#include "native_queries.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "SDL.h"

#include "radial_menu.hpp"
#include "modern_input.hpp"
#include "radial_view.hpp"
#include "radial_text.hpp"
#include "font_textures.hpp"
#include "modern_grapple.hpp"
#include "funcs.h"
#include "recompui/recompui.h"
#include "recompinput/players.h"
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "twine_qol.hpp"
#include "twine_recomp.h"

namespace {

constexpr size_t player_count = 4;
constexpr uint8_t weapon_end = 39;
constexpr uint8_t watch_begin = 33;
constexpr uint8_t watch_end = 37;
constexpr uint8_t gadget_begin = 40;
constexpr uint8_t native_item_end = 55;
constexpr uint8_t item_end = twine::radial::virtual_item_end;
constexpr uint32_t item_table_base = 0x800C469CU;
constexpr uint32_t item_table_stride = 232U;

struct PlayerState {
    twine::qol::TapHold weapon;
    twine::qol::TapHold gadget;
    uint64_t epoch = 0;
    uint64_t weapons = 0;
    uint64_t gadgets = 0;
    twine::radial::PendingSelection pending_weapon;
    twine::radial::PendingSelection pending_gadget;
    twine::radial::PendingSelection pending_special;
    uint8_t selected_gadget = 0xFF;
    uint64_t selection_epoch = 0;
    bool xray_toggle_down = false;
    int selected = -1;
    uint32_t pointer_session = 0;
    twine::radial::Selection selection;
    twine::radial::WatchShortcut watch_shortcut;
    bool open = false;
    bool gadget_open = false;
};

std::array<PlayerState, player_count> players;
std::array<std::atomic<uint64_t>, player_count> published_weapons;
std::array<std::atomic<uint64_t>, player_count> published_gadgets;

std::array<std::atomic<uint16_t>, player_count> published_equipment;
std::array<uint64_t, player_count> logged_weapons{~uint64_t{0}, ~uint64_t{0},
    ~uint64_t{0}, ~uint64_t{0}};
std::array<uint64_t, player_count> logged_gadgets{~uint64_t{0}, ~uint64_t{0},
    ~uint64_t{0}, ~uint64_t{0}};
std::array<std::atomic<uint8_t>, item_end> published_resource;
std::array<std::array<char, 40>, item_end> item_names{};
uint64_t named_items = 0;
std::atomic<int> shown_player{-1};
std::atomic_bool shown_gadget{false};
std::atomic<int> shown_selection{-1};
std::atomic<uint32_t> shown_session{0};
std::atomic<uint64_t> published_pointer{0};
uint32_t next_pointer_session = 0;
recompui::ContextId context = recompui::ContextId::null();
recompui::Element* wheel = nullptr;
recompui::Element* title = nullptr;
std::array<recompui::Element*, weapon_end> labels{};
bool visible = false;
int displayed_player = -1;
bool displayed_gadget = false;
int displayed_selection = -1;
uint64_t displayed_items = 0;
int displayed_width = 0, displayed_height = 0;
std::array<uint8_t, item_end> displayed_resources{};
uint32_t displayed_session = 0;
twine::radial::PointerMotion pointer_motion;
int last_pointer_x = -1, last_pointer_y = -1, last_pointer_item = -1;
bool center_pointer_pending = false;

std::array<uint8_t, weapon_end> owned_items(
    uint64_t mask,
    uint8_t begin,
    uint8_t end,
    size_t& count
) {
    std::array<uint8_t, weapon_end> result{};
    count = 0;
    for (uint8_t item = begin; item < end; ++item) {
        if ((mask & (uint64_t{1} << item)) != 0) {
            result[count++] = item;
        }
    }
    return result;
}

void unpublish(int player) {
    int expected = player;
    shown_player.compare_exchange_strong(
        expected, -1, std::memory_order_release, std::memory_order_relaxed);
}

void close(int player, PlayerState& state) {
    state.open = false;
    state.gadget_open = false;
    state.selected = -1;
    unpublish(player);
}

void load_item_name(uint8_t* rdram, twine::native::Queries& queries, uint8_t item) {
    if ((named_items & (uint64_t{1} << item)) != 0) {
        return;
    }
    const uint32_t text = queries.text(0x6000U | item);
    auto& destination = item_names[item];
    size_t length = 0;
    if (text != 0) {
        while (length + 1 < destination.size() &&
                twine::grapple::rdram_range_valid(text + uint32_t(length), 1)) {
            const uint8_t value = TWINE_MEM_BU(
                static_cast<uint32_t>(length), text);
            if (value == 0) {
                break;
            }
            destination[length++] = value >= 32 && value < 127
                ? static_cast<char>(value) : ' ';
        }
    }
    destination[length] = '\0';
    named_items |= uint64_t{1} << item;
}

bool live_weapon_selectable(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t inventory,
    uint8_t item
) {
    const uint32_t table = item_table_base +
        static_cast<uint32_t>(item) * item_table_stride;
    return twine::radial::weapon_selectable(
        item,
        TWINE_MEM_BU(
            0x1FC + static_cast<uint32_t>(item) * 8U, inventory),
        TWINE_MEM_HU(0, table),
        twine::native::Queries(rdram, ctx).ammo(inventory, item));
}

}

namespace twine::radial {

void initialize_ui() {
    if (context != recompui::ContextId::null()) {
        return;
    }
    view::register_elements();
    context = recompui::create_context();
    context.open();
    context.set_captures_input(false);
    context.set_captures_mouse(true);
    auto* document = context.get_root_element();
    document->set_background_color(recompui::Color{0, 12, 10, 200});

    auto* background = context.create_element<recompui::Element>(document, 0U, "twine-radial-grid");
    background->set_position(recompui::Position::Absolute);
    background->set_left(0);
    background->set_top(0);
    background->set_width(100, recompui::Unit::Percent);
    background->set_height(100, recompui::Unit::Percent);
    background->set_background_color(recompui::Color{5, 28, 24, 128});
    auto* root = context.create_element<recompui::Element>(document);
    wheel = root;
    root->set_position(recompui::Position::Absolute);
    root->set_left(5, recompui::Unit::Percent);
    root->set_top(5, recompui::Unit::Percent);
    root->set_width(90, recompui::Unit::Percent);
    root->set_height(90, recompui::Unit::Percent);
    const bool native_font = bool(twine::fonts::menu_atlas());
    const auto create_label = [&]() -> recompui::Element* {
        if (native_font) return context.create_element<view::Label>(root);
        return context.create_element<recompui::Label>(root, "", recompui::LabelStyle::Small);
    };
    title = create_label();
    title->set_position(recompui::Position::Absolute);
    title->set_left(50, recompui::Unit::Percent);
    title->set_top(50, recompui::Unit::Percent);
    title->set_translate_2D(-50, -50, recompui::Unit::Percent);
    title->set_font_size(24);
    title->set_letter_spacing(0);
    title->set_color(recompui::Color{102, 255, 255, 255});
    title->set_background_color(recompui::Color{0, 15, 12, 240});
    title->set_border_width(2);
    title->set_border_color(recompui::Color{37, 113, 99, 255});
    title->set_padding(12);
    for (size_t i = 0; i < labels.size(); ++i) {
        labels[i] = create_label();
        labels[i]->set_position(recompui::Position::Absolute);
        labels[i]->set_font_size(20);
        labels[i]->set_font_weight(700);
        labels[i]->set_letter_spacing(0);
        labels[i]->set_padding_left(8);
        labels[i]->set_padding_right(8);
        labels[i]->set_padding_top(5);
        labels[i]->set_padding_bottom(5);
        labels[i]->set_max_width(24, recompui::Unit::Percent);
        labels[i]->set_border_width(2);
        labels[i]->set_translate_2D(-50, -50, recompui::Unit::Percent);
    }
    context.close();
}

void update_ui() {
    const int player = shown_player.load(std::memory_order_acquire);
    if (player < 0) {
        if (visible) {
            recompui::hide_context(context);
            visible = false;
        }
        displayed_player = -1;
        return;
    }
    initialize_ui();
    if (!visible) {
        recompui::show_context(context, {});
        visible = true;
    }
    const bool gadget = shown_gadget.load(std::memory_order_relaxed);
    const int selected = shown_selection.load(std::memory_order_relaxed);
    const uint32_t session = shown_session.load(std::memory_order_acquire);
    const uint64_t items = gadget
        ? published_gadgets[player].load(std::memory_order_acquire)
        : published_weapons[player].load(std::memory_order_acquire);
    std::array<uint8_t, item_end> resources{};
    for (size_t i = 0; i < resources.size(); ++i) {
        resources[i] = published_resource[i].load(std::memory_order_relaxed);
    }

    context.open();
    context.set_captures_mouse(player == 0);
    if (displayed_session != session) {
        displayed_session = session;
        pointer_motion = {};
        center_pointer_pending = true;
        last_pointer_x = last_pointer_y = last_pointer_item = -1;
    }
    size_t count = 0;
    const auto owned = owned_items(items, gadget ? watch_begin : 0,
        gadget ? item_end : weapon_end, count);
    SDL_Window* window = SDL_GetKeyboardFocus();
    if (player == 0 && window && SDL_GetRelativeMouseMode() == SDL_FALSE) {
        int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
        if (width > 0 && height > 0 && pixel_width > 0 && pixel_height > 0) {
            if (center_pointer_pending) {
                int before_x = 0, before_y = 0;
                SDL_GetMouseState(&before_x, &before_y);
                pointer_motion.begin(before_x, before_y, width / 2, height / 2);

                SDL_WarpMouseInWindow(window, width / 2, height / 2);
                recompui::activate_mouse();
                center_pointer_pending = false;
                last_pointer_x = before_x;
                last_pointer_y = before_y;
                last_pointer_item = 0xFF;
                published_pointer.store(pointer_sample(session, 0, 0xFF),
                    std::memory_order_release);
            }
            int mouse_x = 0, mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            const Rect bounds{wheel->get_absolute_left(), wheel->get_absolute_top(),
                wheel->get_client_width(), wheel->get_client_height()};
            std::array<Rect, weapon_end> label_bounds{};
            for (size_t i = 0; i < count; ++i) {
                const auto center = item_position(i, count);
                const float w = labels[i]->get_client_width(), h = labels[i]->get_client_height();
                label_bounds[i] = {bounds.x + center.x * bounds.width - w * 0.5f,
                    bounds.y + center.y * bounds.height - h * 0.5f, w, h};
            }
            const int hit = pointer_sector(
                {float(mouse_x) * pixel_width / width, float(mouse_y) * pixel_height / height},
                bounds, std::span(label_bounds.data(), count));
            const int item = hit >= 0 ? owned[hit] : 0xFF;
            const uint32_t motion = pointer_motion.sample(mouse_x, mouse_y);
            if (mouse_x != last_pointer_x || mouse_y != last_pointer_y || item != last_pointer_item) {

                published_pointer.store(pointer_sample(session, motion, uint8_t(item)),
                    std::memory_order_release);
                last_pointer_x = mouse_x;
                last_pointer_y = mouse_y;
                last_pointer_item = item;
            }
        }
    }
    int ui_width = 0, ui_height = 0;
    recompui::get_window_size(ui_width, ui_height);
    if (displayed_player == player && displayed_gadget == gadget &&
        displayed_selection == selected && displayed_items == items &&
        displayed_resources == resources && displayed_width == ui_width && displayed_height == ui_height) {
        context.close();
        return;
    }
    title->set_text(selected < 0 ? "CANCEL" : (gadget ? "GADGETS" : "WEAPONS"));
    const float font_size = count > 28 ? 12 : count > 20 ? 14 : count > 12 ? 16 : 20;
    const float dp = wheel->get_dp_to_pixel_ratio();
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i >= count) {
            labels[i]->set_text("");
            labels[i]->display_hide();
            continue;
        }
        labels[i]->display_show();
        labels[i]->set_font_size(font_size);
        labels[i]->set_max_width(view::label_width(i, count, ui_width * 0.9f, ui_height * 0.9f,
            (font_size * 1.5f + 14) * dp, 6 * dp), recompui::Unit::Px);
        const auto position = item_position(i, count);
        labels[i]->set_left(position.x * 100, recompui::Unit::Percent);
        labels[i]->set_top(position.y * 100, recompui::Unit::Percent);
        const uint8_t item = owned[i];
        std::string text;
        if (item == twine::radial::night_vision_virtual_item) {
            text = twine::rom::metadata().text[static_cast<size_t>(twine::rom::Text::night_vision_name_1)];
        }
        else if (item == twine::radial::xray_virtual_item) {
            text = twine::rom::metadata().text[static_cast<size_t>(twine::rom::Text::xray_name_1)];
        }
        else {
            text = item_names[item][0] != '\0'
                ? item_names[item].data()
                : "Item " + std::to_string(item);
        }
        const uint8_t resource = resources[item];
        if (resource > 1) {
            text += " - " + std::to_string(resource);
        }
        labels[i]->set_text(text);
        const bool highlighted = i == static_cast<size_t>(selected);
        labels[i]->set_color(highlighted ? recompui::Color{0, 12, 8, 255}
            : recompui::Color{102, 255, 255, 255});
        labels[i]->set_background_color(highlighted ? recompui::Color{174, 180, 0, 255}
            : recompui::Color{0, 19, 15, 224});
        labels[i]->set_border_color(highlighted ? recompui::Color{207, 213, 42, 255}
            : recompui::Color{20, 64, 53, 255});
    }
    context.close();
    displayed_player = player;
    displayed_gadget = gadget;
    displayed_selection = selected;
    displayed_items = items;
    displayed_width = ui_width;
    displayed_height = ui_height;
    displayed_resources = resources;
}

InputResult update_input(
    int player,
    bool weapon_down,
    bool gadget_down,
    bool xray_toggle_down,
    uint8_t watch_shortcuts,
    bool cancel_down,
    float direction_x,
    float direction_y,
    int32_t weapon_scroll
) {
    if (player < 0 || static_cast<size_t>(player) >= players.size()) {
        return {};
    }
    auto& state = players[player];
    state.weapons = published_weapons[player].load(std::memory_order_acquire);
    state.gadgets = published_gadgets[player].load(std::memory_order_acquire);
    const uint64_t lifecycle_epoch = twine::qol::lifecycle_epoch();
    if (state.selection_epoch != lifecycle_epoch) {
        state.selection_epoch = lifecycle_epoch;
        state.selected_gadget = 0xFF;
        state.pending_weapon.clear();
        state.pending_gadget.clear();
        state.pending_special.clear();
    }
    if (!owned(state.gadgets, state.selected_gadget)) {
        state.selected_gadget = 0xFF;
    }
    const bool xray_toggle_rising =
        xray_toggle_down && !state.xray_toggle_down;
    state.xray_toggle_down = xray_toggle_down;
    const auto settings = twine::qol::settings();
    const auto weapon_event = settings.weapons == twine::qol::SelectionMode::Radial
        ? state.weapon.update(weapon_down) : twine::qol::PressEvent::None;
    const auto gadget_event = settings.gadgets == twine::qol::SelectionMode::Radial
        ? state.gadget.update(gadget_down) : twine::qol::PressEvent::None;
    const bool was_open = state.open;

    InputResult result{};
    if (weapon_scroll != 0 && !was_open && !weapon_down && !gadget_down && !cancel_down) {
        const uint8_t pending = state.pending_weapon.peek();
        const uint8_t current = pending != 0xFF ? pending :
            twine::modern_input::weapon_family(current_item(player));
        const uint8_t selected = scroll_owned(state.weapons, current, weapon_scroll);
        if (selected != 0xFF) {
            state.pending_gadget.clear();
            state.pending_weapon.queue(selected, lifecycle_epoch);
            result.tap_actions |= 2U;
        }
    }
    result.consumed_buttons = ((watch_shortcuts & 1U) ? 0x0200U : 0U) |
        ((watch_shortcuts & 2U) ? 0x0400U : 0U) |
        ((watch_shortcuts & 4U) ? 0x0100U : 0U);
    const uint8_t quick_item = state.watch_shortcut.update(
        watch_shortcuts, state.gadgets, was_open || weapon_down || gadget_down);
    if (quick_item != 0xFF) {
        state.pending_weapon.clear();
        state.selected_gadget = quick_item;
        state.pending_gadget.queue(quick_item, lifecycle_epoch);
        result.tap_actions |= 2U;
    }
    if (weapon_event == twine::qol::PressEvent::Tap) {
        result.tap_actions |= 2U;
    }
    if (gadget_event == twine::qol::PressEvent::Tap) {
        const uint8_t current = state.selected_gadget != 0xFF
            ? state.selected_gadget
            : equipment_context(player).item;
        const uint8_t next = next_owned(
            state.gadgets, current, watch_begin, item_end);
        if (next != 0xFF) {
            state.selected_gadget = next;
            if (is_virtual_special_item(next)) {
                state.pending_special.queue(next, lifecycle_epoch);
            }
            else {
                state.pending_weapon.clear();
                state.pending_gadget.queue(next, lifecycle_epoch);

                result.tap_actions |= next < gadget_begin ? 2U : 4U;
            }

        }
    }
    const uint8_t equipment = toggle_equipment(state.gadgets, state.selected_gadget);
    result.consume_xray_binding = xray_toggle_down && equipment != 0xFF;
    if (xray_toggle_rising && equipment != 0xFF) {
        state.selected_gadget = equipment;
        state.pending_special.queue(equipment, lifecycle_epoch);

    }
    if (weapon_event == twine::qol::PressEvent::Hold && state.weapons != 0) {
        state.open = true;
        state.gadget_open = false;
        state.epoch = twine::qol::lifecycle_epoch();
        state.selected = -1;
        state.pointer_session = ++next_pointer_session;
        state.selection = {};
    }
    if (gadget_event == twine::qol::PressEvent::Hold && state.gadgets != 0) {
        state.open = true;
        state.gadget_open = true;
        state.epoch = twine::qol::lifecycle_epoch();
        state.selected = -1;
        state.pointer_session = ++next_pointer_session;
        state.selection = {};
    }
    if (!was_open && state.open) {
        const uint64_t items = state.gadget_open ? state.gadgets : state.weapons;

    }
    if (!state.open) {
        return result;
    }
    result.capture = true;
    size_t count = 0;
    const auto items = owned_items(
        state.gadget_open ? state.gadgets : state.weapons,
        state.gadget_open ? watch_begin : 0,
        state.gadget_open ? item_end : weapon_end,
        count);
    const uint64_t pointer = player == 0 ? published_pointer.load(std::memory_order_acquire) : 0;
    const int previous_selection = state.selection.index;
    const uint64_t previous_pointer = state.selection.last_pointer;
    state.selection.update({direction_x, direction_y}, pointer,
        state.pointer_session, std::span(items.data(), count));

    state.selected = state.selection.index;
    shown_gadget.store(state.gadget_open, std::memory_order_relaxed);
    shown_selection.store(state.selected, std::memory_order_relaxed);
    shown_session.store(state.pointer_session, std::memory_order_release);
    shown_player.store(player, std::memory_order_release);
    if (cancel_down || state.epoch != twine::qol::lifecycle_epoch()) {

        close(player, state);
    } else if ((!state.gadget_open && !weapon_down) ||
               (state.gadget_open && !gadget_down)) {
        if (state.selected >= 0 && state.selected < static_cast<int>(count)) {

            if (state.gadget_open) {
                state.selected_gadget = items[state.selected];
                if (is_virtual_special_item(items[state.selected])) {
                    state.pending_special.queue(items[state.selected], lifecycle_epoch);

                }
                else {
                    state.pending_weapon.clear();
                    state.pending_gadget.queue(items[state.selected], lifecycle_epoch);
                    result.tap_actions |= items[state.selected] < gadget_begin
                        ? 2U : 4U;
                }
            } else {
                state.pending_gadget.clear();
                state.pending_weapon.queue(items[state.selected], lifecycle_epoch);
                result.tap_actions |= 2U;
            }
        }

        close(player, state);
    }
    return result;
}

void clear(int player) {
    if (player >= 0 && static_cast<size_t>(player) < players.size()) {
        auto& state = players[player];
        state.weapon.reset();
        state.gadget.reset();
        state.epoch = 0;
        state.weapons = 0;
        state.gadgets = 0;
        state.pending_weapon.clear();
        state.pending_gadget.clear();
        state.pending_special.clear();
        state.selected = -1;
        state.pointer_session = 0;
        state.selection = {};
        state.watch_shortcut = {};
        state.open = false;
        state.gadget_open = false;
        state.xray_toggle_down = false;
        unpublish(player);
    }
}

bool active(int player) {
    return player >= 0 && static_cast<size_t>(player) < players.size() &&
        players[player].open;
}

uint64_t weapon_mask(int player) {
    return player >= 0 && static_cast<size_t>(player) < player_count
        ? published_weapons[player].load(std::memory_order_acquire) : 0;
}

EquipmentContext equipment_context(int player) {
    if (player < 0 || static_cast<size_t>(player) >= player_count) return {};
    const uint16_t value = published_equipment[player].load(std::memory_order_acquire);
    return {uint8_t(value), (value & 0x100U) != 0, (value & 0x200U) != 0};
}

uint8_t current_item(int player) {
    return equipment_context(player).item;
}

uint8_t selected_gadget(int player) {
    return player >= 0 && static_cast<size_t>(player) < player_count
        ? players[player].selected_gadget : 0xFF;
}

bool scoped(int player) {
    return equipment_context(player).scoped;
}

bool direction_for_weapon(int player, uint8_t item, float& x, float& y) {
    if (player < 0 || static_cast<size_t>(player) >= player_count) {
        return false;
    }
    const uint64_t mask = published_weapons[player].load(
        std::memory_order_acquire);
    size_t count = 0;
    const auto items = owned_items(mask, 0, weapon_end, count);
    for (size_t index = 0; index < count; ++index) {
        if (items[index] == item) {
            const float angle = 6.28318530718f *
                static_cast<float>(index) / static_cast<float>(count);
            x = std::sin(angle);
            y = -std::cos(angle);
            return true;
        }
    }
    return false;
}

bool direction_for_gadget(int player, uint8_t item, float& x, float& y) {
    if (player < 0 || static_cast<size_t>(player) >= player_count) {
        return false;
    }
    const uint64_t mask = published_gadgets[player].load(
        std::memory_order_acquire);
    size_t count = 0;
    const auto items = owned_items(mask, watch_begin, item_end, count);
    for (size_t index = 0; index < count; ++index) {
        if (items[index] == item) {
            const float angle = 6.28318530718f *
                static_cast<float>(index) / static_cast<float>(count);
            x = std::sin(angle);
            y = -std::cos(angle);
            return true;
        }
    }
    return false;
}

}

extern "C" void twine_radial_sync(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t player = static_cast<uint32_t>(ctx->r19);
    if (player >= player_count) {
        return;
    }
    const uint32_t inventory = static_cast<uint32_t>(ctx->r17);
    const uint32_t player_object = static_cast<uint32_t>(ctx->r18);
    const uint32_t item_state = TWINE_MEM_W(0x68, player_object);
    uint16_t equipment = 0xFF;
    if (twine::grapple::rdram_range_valid(item_state, 0x10U)) {
        const uint8_t item = TWINE_MEM_BU(0x0E, item_state);
        if (item < 59) {

            const float maximum_zoom = std::bit_cast<float>(uint32_t(TWINE_MEM_W(
                0x70, item_table_base + uint32_t(item) * item_table_stride)));
            equipment = item | ((TWINE_MEM_HU(0x0C, item_state) & 1U) ? 0x100U : 0U) |
                ((std::isfinite(maximum_zoom) && maximum_zoom > 1.0f) ? 0x200U : 0U);
        }
    }
    published_equipment[player].store(equipment, std::memory_order_release);
    uint64_t weapons = 0;
    uint64_t gadgets = 0;
    uint64_t possessions = 0;
    uint64_t ammo_available = 0;
    twine::native::Queries queries(rdram, ctx);
    for (uint8_t item = 0; item < native_item_end; ++item) {
        const uint8_t resource = TWINE_MEM_BU(0x1FC + item * 8, inventory);
        if (resource != 0) {
            possessions |= uint64_t{1} << item;
        }
        const bool has_ammo = resource != 0 && item < weapon_end &&
            queries.ammo(inventory, item);
        if (has_ammo) {
            ammo_available |= uint64_t{1} << item;
        }
        const uint32_t table = item_table_base +
            static_cast<uint32_t>(item) * item_table_stride;
        if (twine::radial::weapon_selectable(
                item, resource, TWINE_MEM_HU(0, table), has_ammo)) {
            weapons |= uint64_t{1} << item;
            load_item_name(rdram, queries, item);
        }
        if (resource != 0 &&
            ((item >= watch_begin && item < watch_end) ||
             item >= gadget_begin)) {
            gadgets |= uint64_t{1} << item;
            load_item_name(rdram, queries, item);
        }
        published_resource[item].store(resource, std::memory_order_relaxed);
    }
    for (uint8_t item : {twine::radial::night_vision_virtual_item, twine::radial::xray_virtual_item}) {
        if (queries.equipment(inventory, twine::radial::special_id_for_virtual_item(item))) {
            gadgets |= uint64_t{1} << item;
        }
    }
    published_resource[twine::radial::night_vision_virtual_item].store(
        twine::radial::owned(
            gadgets, twine::radial::night_vision_virtual_item) ? 1U : 0U,
        std::memory_order_relaxed);
    published_resource[twine::radial::xray_virtual_item].store(
        twine::radial::owned(gadgets, twine::radial::xray_virtual_item)
            ? 1U : 0U,
        std::memory_order_relaxed);
    published_weapons[player].store(weapons, std::memory_order_release);
    published_gadgets[player].store(gadgets, std::memory_order_release);
    if (logged_weapons[player] != weapons || logged_gadgets[player] != gadgets) {

        logged_weapons[player] = weapons;
        logged_gadgets[player] = gadgets;
    }
}

extern "C" void twine_radial_apply_weapon(
    uint8_t* rdram, recomp_context* ctx
) {
    const uint32_t inventory = static_cast<uint32_t>(ctx->r20);
    const uint32_t player = TWINE_MEM_BU(0x182, inventory);
    if (player >= player_count) {
        return;
    }
    auto& state = players[player];
    const uint8_t gadget = state.pending_gadget.peek();
    const bool watch = gadget >= watch_begin && gadget < watch_end;
    if (state.pending_weapon.peek() == 0xFF && !watch) {
        return;
    }
    const uint8_t item = (watch ? state.pending_gadget : state.pending_weapon)
        .take(twine::qol::lifecycle_epoch());
    if (item == 0xFF) { return; }
    const uint64_t current = watch
        ? published_gadgets[player].load(std::memory_order_acquire)
        : published_weapons[player].load(std::memory_order_acquire);
    const bool live_selectable = watch
        ? item >= watch_begin && item < watch_end &&
            TWINE_MEM_BU(0x1FC + static_cast<uint32_t>(item) * 8U,
                inventory) != 0
        : live_weapon_selectable(rdram, ctx, inventory, item);
    if (twine::radial::owned(current, item) && live_selectable) {
        const uint32_t item_state = TWINE_MEM_W(0x68, ctx->r17);
        TWINE_MEM_B(0x0F, item_state) = item;

    }
}

extern "C" void twine_radial_prepare_gadget(
    uint8_t* rdram, recomp_context* ctx
) {
    const uint32_t player_object = static_cast<uint32_t>(ctx->r16);
    const uint32_t inventory = TWINE_MEM_W(0x6C, player_object);
    const uint32_t player = TWINE_MEM_BU(0x182, inventory);
    if (player >= player_count) {
        return;
    }
    auto& state = players[player];

    if (state.pending_gadget.peek() < gadget_begin) { return; }
    const uint8_t item = state.pending_gadget.take(twine::qol::lifecycle_epoch());
    if (item >= gadget_begin && item < native_item_end &&
        twine::radial::owned(
            published_gadgets[player].load(std::memory_order_acquire), item) &&
        TWINE_MEM_BU(0x1FC + static_cast<uint32_t>(item) * 8U,
            inventory) != 0) {

        const uint32_t item_state = TWINE_MEM_W(0x68, player_object);
        TWINE_MEM_B(0x0F, item_state) = item == gadget_begin
            ? native_item_end - 1 : item - 1;

    }
}

extern "C" void twine_radial_apply_special(
    uint8_t* rdram, recomp_context* ctx
) {
    const uint32_t player = static_cast<uint32_t>(ctx->r19);
    const uint32_t inventory = static_cast<uint32_t>(ctx->r17);
    if (player >= player_count ||
            !twine::grapple::rdram_range_valid(inventory, 0x410U)) {
        return;
    }
    auto& state = players[player];
    const uint8_t item = state.pending_special.take(twine::qol::lifecycle_epoch());
    if (!twine::radial::is_virtual_special_item(item)) {
        return;
    }

    const uint8_t special_id =
        twine::radial::special_id_for_virtual_item(item);
    if (!twine::native::Queries(rdram, ctx).equipment(inventory, special_id)) {

        return;
    }

    const uint8_t before = TWINE_MEM_BU(0x185, inventory);
    const recomp_context saved = *ctx;
    ctx->r4 = static_cast<int32_t>(inventory);
    if (item == twine::radial::night_vision_virtual_item) {
        func_800702AC(rdram, ctx);
    }
    else {
        func_80070528(rdram, ctx);
    }
    *ctx = saved;
    const uint32_t root = TWINE_MEM_W(
        static_cast<uint32_t>(player * 4U), 0x80109688U);
    const uint32_t view_flags =
        twine::grapple::rdram_range_valid(root, 0x104U)
            ? TWINE_MEM_W(0x100, root) : 0U;

}

extern "C" uint32_t twine_radial_should_freeze(uint8_t*, recomp_context*) {
    const int player = shown_player.load(std::memory_order_acquire);
    return player >= 0 && recompinput::players::is_single_player_mode();
}

twine::state::Bytes twine::state::capture_radial() {
    Writer out; out.u32(1);
    for (auto& player : players) out.fields(player.selected_gadget, player.selection_epoch == qol::lifecycle_epoch());
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_radial(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid radial state schema");
    std::array<uint8_t, player_count> selections; std::array<bool, player_count> valid;
    for (size_t i = 0; i < player_count; ++i) {
        in.fields(selections[i], valid[i]);
        if (selections[i] != 0xff && selections[i] >= item_end) throw std::runtime_error("Invalid radial gadget state");
    }
    in.end();
    return prepared_owner([selections, valid]() noexcept {
        for (size_t i = 0; i < player_count; ++i) {
            radial::clear(int(i));
            players[i].selected_gadget = selections[i];
            players[i].selection_epoch = valid[i] ? qol::lifecycle_epoch() : 0;
            published_equipment[i].store(0xFF);
        }
    });
}
