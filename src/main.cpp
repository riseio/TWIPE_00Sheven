#include "save_state_shortcuts.hpp"
#include "save_state_owner.hpp"
#include "save_state_service.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cinttypes>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define SDL_MAIN_HANDLED
#include "SDL.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <io.h>
#include <share.h>
#include "SDL_syswm.h"
#endif

#include "pfs_runtime.hpp"
#include "audio_host.hpp"
#include "audio_status_ui.hpp"
#include "audio_settings.hpp"
#include "campaign_profile.hpp"
#include "cheats.hpp"
#include "aspect_layout.hpp"
#include "hud_layout.hpp"
#include "input_prompts.hpp"
#include "modern_input.hpp"
#include "gameplay_input_owner.hpp"
#include "modern_grapple.hpp"
#include "world_render_context.hpp"
#include "native_ui_render.hpp"
#include "native_options.hpp"
#include "pause_menu.hpp"
#include "font_textures.hpp"
#include "modern_textures.hpp"
#include "objectives_overlay.hpp"
#include "steam_deck_grips.hpp"
#include "radial_menu.hpp"
#include "resume_recovery.hpp"
#include "radial_runtime.hpp"
#include "sniper_controls.hpp"
#include "vision_battery.hpp"
#include "sprint.hpp"
#include "keyboard_controls.hpp"
#include "health_regeneration.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/rsp.hpp"
#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/profiles.h"
#include "recompinput/recompinput.h"
#include "recompui/config.h"
#include "recompui/program_config.h"
#include "recompui/recompui.h"
#include "recompui/renderer.h"
#include "ultramodern/ultramodern.hpp"

#include "util/file.h"
#include "util/steam_deck.h"
#include "rt64_extended_gbi.h"
#include "shared/rt64_modern_grapple.h"
#include "funcs.h"
#include "local_aot.hpp"

#ifndef TWINE_VERSION_MAJOR
#define TWINE_VERSION_MAJOR 0
#define TWINE_VERSION_MINOR 0
#define TWINE_VERSION_PATCH 0
#endif

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* context);
extern RspUcodeFunc twine_audio;
extern std::vector<recomp::GameEntry> supported_games;

SDL_Window* window = nullptr;

namespace {

constexpr std::u8string_view game_id = u8"twine.n64.us.1.0";
constexpr uint32_t initial_overlay_staging = 0x800D7050U;
constexpr uint32_t initial_overlay_staging_size = 0x22000U;
constexpr char mouse_acceleration_option[] = "mouse_acceleration";
constexpr char joystick_sensitivity_x_option[] = "joystick_sensitivity_x";
constexpr char joystick_sensitivity_y_option[] = "joystick_sensitivity_y";
constexpr char auto_aim_option[] = "auto_aim";
constexpr size_t modern_input_player_count = 4;
constexpr uint32_t pause_menu_definition = 0x800BDB1CU;
constexpr uint32_t frontend_menu_definition = 0x800BD8C4U;
constexpr uint32_t mission_select_definition = 0x800BD9B4U;
constexpr uint32_t debrief_menu_definition = 0x800BD9F0U;
bool valid_rdram_address(uint32_t address, uint32_t size) {
    constexpr uint32_t rdram_size = 8U * 1024U * 1024U;
    const uint32_t segment = address & 0xE0000000U;
    const uint32_t physical = address & 0x1FFFFFFFU;
    return (segment == 0x80000000U || segment == 0xA0000000U) &&
        size <= rdram_size && physical <= rdram_size - size;
}

struct AtomicModernInput {
    std::atomic<float> forward{0.0f};
    std::atomic<float> strafe_right{0.0f};
    std::atomic<float> look_right{0.0f};
    std::atomic<float> look_up{0.0f};

    void store(const twine::modern_input::State& state) {
        forward.store(state.forward, std::memory_order_relaxed);
        strafe_right.store(state.strafe_right, std::memory_order_relaxed);
        look_right.store(state.look_right, std::memory_order_relaxed);
        look_up.store(state.look_up, std::memory_order_relaxed);
    }

    twine::modern_input::State load() const {
        return {
            forward.load(std::memory_order_relaxed),
            strafe_right.load(std::memory_order_relaxed),
            look_right.load(std::memory_order_relaxed),
            look_up.load(std::memory_order_relaxed),
        };
    }
};

std::array<AtomicModernInput, modern_input_player_count> modern_inputs;
std::array<twine::modern_input::PitchState, modern_input_player_count>
    modern_pitch_states;
std::array<std::atomic<uint8_t>, modern_input_player_count>
    pending_weapon_categories;
std::array<std::atomic<uint8_t>, modern_input_player_count>
    held_modern_actions;
std::array<std::atomic<uint8_t>, modern_input_player_count>
    pending_modern_actions;
std::array<twine::sniper::ScopeIntent, modern_input_player_count> sniper_scope_intents;
std::array<twine::sniper::ZoomRouting, modern_input_player_count> sniper_zoom_routing;
std::atomic<uint64_t> modern_vi_frame{0};
std::atomic<uint64_t> last_gameplay_input_frame{0};
twine::modern_input::GameplayInputOwner gameplay_input_owner;
std::atomic_bool deck_face_a_requested{false};
std::atomic_bool deck_face_b_requested{false};
std::atomic_int32_t deck_controller_instance{-1};

std::atomic<uint8_t*> runtime_rdram{nullptr};
std::atomic_bool instant_pause_closing_requested{false};
twine::qol::AttractSession attract_session;
thread_local bool instant_pause_opening = false;
thread_local bool complete_pause_movie = false;

int16_t current_overlay(uint8_t* rdram) {
    return rdram == nullptr ? 0 : TWINE_MEM_H(0, 0x80117386U);
}

uint8_t current_demo_mode(uint8_t* rdram) {
    return rdram == nullptr ? 0 : TWINE_MEM_BU(0XEEB0, 0X800C0000U);
}

int16_t current_native_menu_page(uint8_t* rdram) {
    if (rdram == nullptr) {
        return -1;
    }
    const uint32_t menu = TWINE_MEM_W(0X2F2C, 0X80100000U);
    return valid_rdram_address(menu, 0X7EU)
        ? TWINE_MEM_H(0X7A, menu) : -1;
}

bool gameplay_mapping_active(uint8_t* rdram) {
    return twine::modern_input::gameplay_mapping_active(
        current_overlay(rdram), current_demo_mode(rdram),
        current_native_menu_page(rdram) < 0 &&
            gameplay_input_owner.active(twine::qol::lifecycle_epoch()));
}

bool quit_confirmation_active(uint8_t* rdram) {
    if (rdram == nullptr) {
        return false;
    }
    const uint32_t menu = TWINE_MEM_W(0X2F2C, 0X80100000U);
    const uint32_t manager = TWINE_MEM_W(0X9BB0, 0X80100000U);
    const bool menu_present = valid_rdram_address(menu, 0X80U) &&
        valid_rdram_address(manager, 0X34U);
    return twine::pause_menu::quit_confirmation_owned_by_modern_controls(
        menu_present,
        menu_present ? TWINE_MEM_H(0X7A, menu) : -1,
        current_demo_mode(rdram),
        menu_present
            ? static_cast<uint32_t>(TWINE_MEM_W(0XC, manager)) : 0,
        menu_present
            ? static_cast<uint32_t>(TWINE_MEM_W(0X10, manager)) : 0,
        menu_present ? TWINE_MEM_HU(0X14, manager) : 0,
        pause_menu_definition,
        frontend_menu_definition,
        debrief_menu_definition);
}

twine::pause_menu::Context pause_menu_context(uint8_t* rdram) {
    twine::pause_menu::Context context;
    context.session = rdram != nullptr && current_overlay(rdram) > 1 &&
        current_demo_mode(rdram) == 0;
    if (!context.session) {
        return context;
    }
    const uint32_t menu = TWINE_MEM_W(0X2F2C, 0X80100000U);
    const uint32_t manager = TWINE_MEM_W(0X9BB0, 0X80100000U);
    context.menu_present = valid_rdram_address(menu, 0X80U) &&
        valid_rdram_address(manager, 0X34U);
    if (!context.menu_present || TWINE_MEM_H(0X7A, menu) != 10 ||
            static_cast<uint32_t>(TWINE_MEM_W(0XC, manager)) !=
                pause_menu_definition) {
        return context;
    }
    const uint16_t mode = TWINE_MEM_HU(0X14, manager);
    context.root = mode == 1;
    context.selection = TWINE_MEM_BU(0X8, manager);
    return context;
}

void store_modern_input(
    int controller,
    const twine::modern_input::State& state
) {
    if (controller >= 0 &&
        static_cast<size_t>(controller) < modern_inputs.size()) {
        modern_inputs[controller].store(state);
    }
}

void store_weapon_category(
    int controller,
    twine::modern_input::WeaponCategory category
) {
    if (controller >= 0 &&
        static_cast<size_t>(controller) <
            pending_weapon_categories.size()) {
        pending_weapon_categories[controller].store(
            static_cast<uint8_t>(category),
            std::memory_order_relaxed);
    }
}

bool modern_input_active(
    int controller,
    recompinput::GameInput input
) {
    if (controller < 0) {
        return false;
    }
    const std::array profiles{
        recompinput::profiles::get_sp_keyboard_profile_index(),
        recompinput::profiles::get_sp_controller_profile_index(),
    };
    for (int profile : profiles) {
        for (size_t binding = 0;
             binding < recompinput::num_bindings_per_input;
             ++binding) {
            if (recompinput::get_input_digital(
                    controller,
                    recompinput::profiles::get_input_binding(
                        profile,
                        input,
                        binding))) {
                return true;
            }
        }
    }
    return false;
}

void clear_modern_actions(int controller) {
    if (controller >= 0 &&
        static_cast<size_t>(controller) < pending_modern_actions.size()) {
        held_modern_actions[controller].store(0, std::memory_order_relaxed);
        pending_modern_actions[controller].store(0, std::memory_order_relaxed);
        sniper_zoom_routing[controller] = {};
    }
}

bool store_modern_actions(
    int controller,
    uint16_t buttons,
    bool cycle_mode,
    bool toggle_lighting,
    bool zoom_in,
    bool zoom_out
) {
    if (controller < 0 ||
        static_cast<size_t>(controller) >= pending_modern_actions.size()) {
        return false;
    }
    const uint8_t current =
        twine::modern_input::action_mask(
            buttons,
            cycle_mode,
            toggle_lighting,
            zoom_in,
            zoom_out);
    const uint8_t previous = held_modern_actions[controller].exchange(
        current,
        std::memory_order_relaxed);
    const uint8_t rising =
        twine::modern_input::rising_actions(current, previous);
    constexpr uint8_t edge_actions = 0x07U;
    pending_modern_actions[controller].fetch_or(
        rising & edge_actions,
        std::memory_order_relaxed);
    return (rising & twine::modern_input::action_bit(
        twine::modern_input::Action::ToggleLighting)) != 0;
}

void twine_on_init(uint8_t* rdram, recomp_context*) {
    twine::textures::load_selected();
    twine::fonts::reset();
    RT64::resetModernRenderTasks();
    runtime_rdram.store(rdram, std::memory_order_release);
    twine::cheats::initialize(rdram);
    unload_overlays(
        static_cast<int32_t>(initial_overlay_staging),
        initial_overlay_staging_size);
}

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    if (SDL_InitSubSystem(
            SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK |
            SDL_INIT_HAPTIC) != 0) {
        throw std::runtime_error(
            std::string("Failed to initialize SDL: ") + SDL_GetError());
    }
    return nullptr;
}

ultramodern::renderer::WindowHandle create_window(
    ultramodern::gfx_callbacks_t::gfx_data_t
) {
    int width = 1600;
    int height = 900;
    SDL_Rect usable_bounds{};
    if (SDL_GetDisplayUsableBounds(0, &usable_bounds) == 0 &&
            usable_bounds.w > 0 && usable_bounds.h > 0) {
        width = std::min(width, usable_bounds.w);
        height = std::min(height, usable_bounds.h);
    }
    window = SDL_CreateWindow(
        "The World Is Probably Enough",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        throw std::runtime_error(
            std::string("Failed to create window: ") + SDL_GetError());
    }

#ifdef _WIN32
    SDL_SysWMinfo window_info{};
    SDL_VERSION(&window_info.version);
    if (SDL_GetWindowWMInfo(window, &window_info) != SDL_TRUE) {
        throw std::runtime_error(
            std::string("Failed to query window: ") + SDL_GetError());
    }
    return {window_info.info.win.window, GetCurrentThreadId()};
#else
    return {window};
#endif
}

void update_gfx(void*) {
    twine::native_options::service_ui_request();
    const bool resume_gap = twine::resume_recovery::detect_clock_gap();
    if (resume_gap) {
        recompinput::request_resume_recovery("graphics_clock_gap");
        twine::deck_grips::recover_after_resume();
    }
    const auto grip_edges = twine::deck_grips::poll();
    const int32_t exact_deck_instance =
        twine::deck_grips::controller_instance();
    const int32_t active_controller_instance =
        recompinput::get_last_active_controller_id();
    const int32_t controller_instance =
        twine::deck_grips::resolve_physical_face_owner(
            twine::deck_grips::physical_faces_available(),
            exact_deck_instance,
            active_controller_instance);
    deck_controller_instance.store(
        controller_instance, std::memory_order_release);
    recompui::set_prompt_physical_face_owner(controller_instance);
    if (controller_instance >= 0 && (grip_edges.a || grip_edges.b)) {
        if (recompui::is_prompt_open()) {
            recompui::activate_prompt_action(
                grip_edges.a
                    ? recompui::MenuAction::Accept
                    : recompui::MenuAction::Back,
                true);
        }
        else if (quit_confirmation_active(
                runtime_rdram.load(std::memory_order_acquire))) {
            deck_face_a_requested.store(
                grip_edges.a, std::memory_order_release);
            deck_face_b_requested.store(
                grip_edges.b, std::memory_order_release);
        }
    }
    else if (controller_instance < 0) {
        deck_face_a_requested.store(false, std::memory_order_release);
        deck_face_b_requested.store(false, std::memory_order_release);
    }
    if (grip_edges.r4) {
        recompinput::request_lighting_menu_toggle("deck_hid");
    }
    if (grip_edges.l4) {
        recompinput::request_modern_lighting_toggle("deck_hid");
    }

    const bool sdl_resume = recompinput::handle_events();
    twine::audio_status::update(sdl_resume || resume_gap);
    twine::state::update_shortcuts(grip_edges);
    if (sdl_resume && !resume_gap) {
        twine::deck_grips::recover_after_resume();
    }
    twine::input_prompts::update_ui();
    twine::radial::update_ui();
    twine::objectives::update_ui();
    twine::vision_battery::update_ui();
}

ultramodern::input::connected_device_info_t connected_device(int controller) {
    const bool connected =
        (recompinput::players::is_single_player_mode() && controller == 0) ||
        (!recompinput::players::is_single_player_mode() &&
         recompinput::players::get_player_is_assigned(controller));
    return {
        connected ? ultramodern::input::Device::Controller
                  : ultramodern::input::Device::None,
        connected ? ultramodern::input::Pak::RumblePak
                  : ultramodern::input::Pak::None,
    };
}

bool get_input(int controller, uint16_t* buttons, float* x, float* y) {
    twine::sprint::InputPublication sprint_input(controller);
    recompinput::set_right_analog_suppressed(true);
    bool result =
        recompinput::profiles::get_n64_input(controller, buttons, x, y);
    recompinput::set_right_analog_suppressed(false);
    const bool input_disabled = recompinput::game_input_disabled();

    bool native_pause_action = false;
    if (controller == 0) {
        static bool pause_start_down = false;
        static bool pause_accept_down = false;
        static uint64_t pause_epoch = twine::qol::lifecycle_epoch();
        const bool start_down = (*buttons & 0X1000U) != 0;
        const bool accept_down =
            (*buttons & twine::modern_input::button_a) != 0;
        bool shortcut_down = modern_input_active(
            controller, recompinput::GameInput::OBJECTIVES);
        const auto pause_context = pause_menu_context(
            runtime_rdram.load(std::memory_order_acquire));
        const uint64_t current_pause_epoch = twine::qol::lifecycle_epoch();
        if (pause_epoch != current_pause_epoch || !pause_context.session) {
            pause_epoch = current_pause_epoch;
            pause_start_down = start_down;
            pause_accept_down = accept_down;
            instant_pause_closing_requested.store(
                false, std::memory_order_release);
        }

        const bool start_pressed = start_down && !pause_start_down;
        const bool accept_pressed = accept_down && !pause_accept_down;
        pause_start_down = start_down;
        pause_accept_down = accept_down;
        if (result && twine::pause_menu::shortcut_input_available(
                input_disabled, pause_context) &&
                recompinput::players::is_single_player_mode() &&
                pause_context.session) {
            if (pause_context.menu_present &&
                    (start_pressed || (pause_context.root &&
                        pause_context.selection == 0 && accept_pressed))) {
                instant_pause_closing_requested.store(
                    true, std::memory_order_release);
            }
        }
        twine::objectives::update_input(
            result && !input_disabled && !pause_context.menu_present &&
                pause_context.session && recompinput::players::is_single_player_mode() &&
                gameplay_mapping_active(runtime_rdram.load(std::memory_order_acquire)),
            shortcut_down, current_pause_epoch);
    }
    const int32_t mouse_wheel = controller == 0
        ? recompinput::consume_mouse_wheel_delta() : 0;
    uint8_t* input_rdram = runtime_rdram.load(std::memory_order_acquire);
    if (quit_confirmation_active(input_rdram)) {
        const bool semantic_accept = modern_input_active(
            controller, recompinput::GameInput::ACCEPT_MENU);
        const bool semantic_back = modern_input_active(
            controller, recompinput::GameInput::BACK_MENU);
        const int32_t physical_controller_instance =
            deck_controller_instance.load(std::memory_order_acquire);
        int32_t assigned_controller_instance = -1;
        if (controller >= 0 && static_cast<size_t>(controller) <
                recompinput::players::get_max_number_of_players()) {
            auto access = recompinput::lock_controller(-1, controller);
            if (access.get() != nullptr) {
                assigned_controller_instance = SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(access.get()));
            }
        }

        const bool physical_face_owner =
            twine::deck_grips::logical_controller_owns_physical_faces(
                recompinput::players::is_single_player_mode(),
                controller,
                physical_controller_instance,
                assigned_controller_instance);
        *buttons = twine::modern_input::remap_pause_quit_confirmation_buttons(
            *buttons,
            semantic_accept,
            semantic_back,
            physical_face_owner,
            physical_face_owner && deck_face_a_requested.exchange(
                false, std::memory_order_acq_rel),
            physical_face_owner && deck_face_b_requested.exchange(
                false, std::memory_order_acq_rel));

        native_pause_action = true;

    }
    if (!result || input_disabled) {
        store_modern_input(controller, {});
        clear_modern_actions(controller);
        twine::radial::clear(controller);
        store_weapon_category(
            controller,
            twine::modern_input::WeaponCategory::None);
        return result;
    }
    if (native_pause_action) {

        store_modern_input(controller, {});
        clear_modern_actions(controller);
        twine::radial::clear(controller);
        store_weapon_category(
            controller,
            twine::modern_input::WeaponCategory::None);
        return result;
    }

    const uint64_t frame = modern_vi_frame.load(std::memory_order_relaxed);
    const int16_t overlay = current_overlay(input_rdram);
    const bool gameplay_mapping = gameplay_mapping_active(input_rdram);

    static std::array<
        twine::modern_input::KeyboardMenuAcceptLatch,
        4> keyboard_menu_accept_latches{};
    if (controller < static_cast<int>(keyboard_menu_accept_latches.size()) &&
            gameplay_mapping) {
        keyboard_menu_accept_latches[controller].observe_gameplay(
            (*buttons & twine::modern_input::button_start) != 0);
    }
    if (!gameplay_mapping && !twine::radial::active(controller)) {
        bool semantic_accept = modern_input_active(
            controller, recompinput::GameInput::ACCEPT_MENU);
        bool semantic_back = modern_input_active(
            controller, recompinput::GameInput::BACK_MENU);
        if (recompui::get_cont_active()) {
            *buttons = twine::modern_input::remap_controller_menu_buttons(
                *buttons,
                semantic_accept,
                semantic_back);
        }
        else {
            const bool native_start_context =
                current_demo_mode(input_rdram) != 0 ||
                current_native_menu_page(input_rdram) < 0 ||

                current_native_menu_page(input_rdram) == 24;
            const auto accept = keyboard_menu_accept_latches[controller]
                .update_frontend(
                    semantic_accept,
                    (*buttons & twine::modern_input::button_start) != 0,
                    native_start_context);
            *buttons = twine::modern_input::remap_keyboard_menu_buttons(
                *buttons,
                accept.accept,
                semantic_back,
                native_start_context);
            if (accept.suppress_start) {
                *buttons = static_cast<uint16_t>(
                    *buttons & ~twine::modern_input::button_start);
            }
        }

        twine::modern_input::apply_menu_dpad(*buttons, *x, *y);
        store_modern_input(controller, {});
        clear_modern_actions(controller);
        twine::radial::clear(controller);
        store_weapon_category(
            controller,
            twine::modern_input::WeaponCategory::None);
        return result;
    }

    float aim_x = 0.0f;
    float aim_y = 0.0f;
    recompinput::get_right_analog(controller, &aim_x, &aim_y);
    float radial_x = aim_x;
    float radial_y = aim_y;
    if (std::hypot(radial_x, radial_y) < 0.35f) {
        radial_x = *x;
        radial_y = -*y;
    }
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    if (controller == 0) {
        recompinput::get_mouse_deltas(&mouse_x, &mouse_y);
    }
    const auto equipment = twine::radial::equipment_context(controller);
    const bool zoom_in_down = modern_input_active(controller, recompinput::GameInput::ZOOM_IN);
    const bool zoom_out_down = modern_input_active(controller, recompinput::GameInput::ZOOM_OUT);
    const auto scope_zoom = controller >= 0 &&
            static_cast<size_t>(controller) < sniper_zoom_routing.size()
        ? sniper_zoom_routing[controller].update(twine::qol::lifecycle_epoch(),
            equipment.zoom_available, equipment.scoped,
            (*buttons & twine::modern_input::button_l) != 0,
            zoom_in_down, zoom_out_down, *y)
        : twine::sniper::ZoomInput{};
    const bool scope_zoom_in = scope_zoom.consume_in;
    const bool scope_zoom_out = scope_zoom.consume_out;
    uint8_t watch_shortcuts =
        (modern_input_active(controller, recompinput::GameInput::WATCH_LASER) ? 1U : 0U) |
        (!scope_zoom_out && modern_input_active(controller, recompinput::GameInput::WATCH_GRAPPLE) ? 2U : 0U) |
        (modern_input_active(controller, recompinput::GameInput::WATCH_DART) ? 4U : 0U);
    bool xray_toggle_down = !scope_zoom_in && modern_input_active(
        controller, recompinput::GameInput::TOGGLE_XRAY);
    const auto radial = twine::radial::update_input(
        controller,
        (*buttons & twine::modern_input::button_a) != 0,
        (*buttons & twine::modern_input::button_r) != 0,
        xray_toggle_down,
        watch_shortcuts,
        (*buttons & twine::modern_input::button_b) != 0,
        radial_x,
        radial_y);
    *buttons &= static_cast<uint16_t>(~radial.consumed_buttons);
    if (radial.consume_xray_binding) {

        *buttons &= static_cast<uint16_t>(
            ~twine::modern_input::dpad_up);
    }
    twine::grapple::handle_input(controller, *buttons);
    const bool zoom_in = scope_zoom.in || (!radial.consume_xray_binding &&
            modern_input_active(
                controller, recompinput::GameInput::ZOOM_IN)) ||
        mouse_wheel > 0;
    const bool zoom_out = scope_zoom.out || ((watch_shortcuts & 2U) == 0 && modern_input_active(
            controller, recompinput::GameInput::ZOOM_OUT)) ||
        mouse_wheel < 0;
    *buttons = twine::modern_input::route_scoped_dpad(
        *buttons, scope_zoom.owns_vertical || scope_zoom_in || scope_zoom_out,
        zoom_in || zoom_out);
    uint16_t action_buttons = *buttons;
    const auto qol_settings = twine::qol::settings();
    if (qol_settings.weapons == twine::qol::SelectionMode::Radial) {
        action_buttons &= static_cast<uint16_t>(~twine::modern_input::button_a);
    }
    if (qol_settings.gadgets == twine::qol::SelectionMode::Radial) {
        action_buttons &= static_cast<uint16_t>(~twine::modern_input::button_r);
    }
    store_weapon_category(controller, radial.capture
        ? twine::modern_input::WeaponCategory::None
        : twine::modern_input::weapon_category(*buttons));
    if (modern_input_active(controller, recompinput::GameInput::RELOAD)) {
        *buttons |= twine::modern_input::button_b;
    }
    bool cycle_mode_down = modern_input_active(
        controller, recompinput::GameInput::CYCLE_MODE);
    if (store_modern_actions(
        controller,
        action_buttons,
        cycle_mode_down,
        modern_input_active(
            controller,
            recompinput::GameInput::TOGGLE_LIGHTING),
        zoom_in,
        zoom_out)) {
        recompinput::request_modern_lighting_toggle("binding");
    }
    if (radial.tap_actions != 0 && controller >= 0 &&
        static_cast<size_t>(controller) < pending_modern_actions.size()) {
        pending_modern_actions[controller].fetch_or(
            radial.tap_actions, std::memory_order_relaxed);
    }
    if (radial.capture) {
        store_modern_input(controller, {});
        *buttons = 0;
        *x = 0.0f;
        *y = 0.0f;
        return result;
    }

    const bool mouse_acceleration = std::get<bool>(
        recompui::config::get_general_config().get_option_value(
            mouse_acceleration_option));
    const twine::modern_input::State mapped = twine::modern_input::compose(
        *x,
        scope_zoom.forward,
        aim_x,
        aim_y,
        mouse_x,
        mouse_y,
        mouse_acceleration,
        static_cast<float>(std::get<double>(recompui::config::get_general_config().get_option_value(joystick_sensitivity_x_option)) / 100.0),
        static_cast<float>(std::get<double>(recompui::config::get_general_config().get_option_value(joystick_sensitivity_y_option)) / 100.0));
    twine::modern_input::State effective = mapped;
    if (twine::grapple::active(controller)) {
        effective.forward = 0.0f;
        effective.strafe_right = 0.0f;
    }
    store_modern_input(controller, effective);
    sprint_input.held = (action_buttons & twine::modern_input::button_b) != 0;
    sprint_input.dedicated = modern_input_active(controller, recompinput::GameInput::SPRINT);
    sprint_input.forward = effective.forward > 0.0f;

    *buttons = twine::modern_input::remap_buttons(*buttons);

    *x = 0.0f;
    *y = 0.0f;
    return result;
}

void configure_modern_controls() {
    using recompinput::GameInput;
    using recompinput::InputField;

    recompinput::set_game_input_name(GameInput::X_AXIS_NEG, "Move Left");
    recompinput::set_game_input_name(GameInput::X_AXIS_POS, "Move Right");
    recompinput::set_game_input_name(GameInput::Y_AXIS_POS, "Move Forward");
    recompinput::set_game_input_name(GameInput::Y_AXIS_NEG, "Move Back");
    recompinput::set_game_input_name(GameInput::A, "Cycle Weapon");
    recompinput::set_game_input_name(GameInput::B, "Action / Interact");
    recompinput::set_game_input_name(GameInput::Z, "Fire / Use Gadget");
    recompinput::set_game_input_name(GameInput::SAVE_STATE, "Save State (F5 / L5)");
    recompinput::set_game_input_name(GameInput::RESTORE_STATE, "Load State (F8 / R5)");
    recompinput::set_game_input_name(GameInput::L, "Aim");
    recompinput::set_game_input_name(GameInput::R, "Cycle Gadget");
    recompinput::set_game_input_name(GameInput::RELOAD, "Reload");
    recompinput::set_game_input_name(
        GameInput::CYCLE_MODE, "Cycle Weapon / Gadget Mode");
    recompinput::set_game_input_name(GameInput::ZOOM_IN, "Zoom In");
    recompinput::set_game_input_name(GameInput::ZOOM_OUT, "Zoom Out");
    recompinput::set_game_input_name(
        GameInput::TOGGLE_LIGHTING, "Toggle Modern Textures");
    recompinput::set_game_input_name(
        GameInput::TOGGLE_XRAY, "Toggle Night Vision / X-ray");
    recompinput::set_game_input_name(
        GameInput::OBJECTIVES, "Toggle Objectives");
    recompinput::set_game_input_name(GameInput::C_UP, "Stand / Jump");
    recompinput::set_game_input_name(GameInput::C_DOWN, "Crouch");
    recompinput::set_game_input_name(GameInput::C_LEFT, "Cycle Handguns");
    recompinput::set_game_input_name(
        GameInput::C_RIGHT, "Cycle Submachine Guns");
    recompinput::set_game_input_name(GameInput::DPAD_LEFT, "Cycle Rifles");
    recompinput::set_game_input_name(GameInput::DPAD_RIGHT, "Cycle Shotgun");
    recompinput::set_game_input_name(GameInput::DPAD_UP, "Cycle Explosives");
    recompinput::set_game_input_name(
        GameInput::DPAD_DOWN, "Cycle Miscellaneous");

    recompinput::set_game_input_description(
        GameInput::X_AXIS_NEG,
        "WASD or the left stick provides independent forward and strafe movement.");
    recompinput::set_game_input_description(
        GameInput::C_UP,
        "Space or the south controller button stands, jumps, or climbs.");
    recompinput::set_game_input_description(
        GameInput::Z,
        "Left mouse or the right trigger fires.");
    recompinput::set_game_input_description(
        GameInput::L,
        "Right mouse or the left trigger aims.");
    recompinput::set_game_input_description(
        GameInput::RELOAD,
        "R reloads; the west controller button uses the game's contextual interact/reload action.");
    recompinput::set_game_input_description(
        GameInput::ZOOM_IN,
        "Z, mouse wheel up, a mouse side button, or D-Pad Up zooms in while aiming with equipment that supports zoom. Hold Aim and move forward to zoom in.");
    recompinput::set_game_input_description(
        GameInput::ZOOM_OUT,
        "X, mouse wheel down, a mouse side button, or D-Pad Down zooms out while aiming with equipment that supports zoom. Hold Aim and move backward to zoom out.");
    recompinput::set_game_input_description(
        GameInput::TOGGLE_XRAY,
        "T or D-Pad Up toggles the owned goggles immediately. Wheel selection also toggles; Fire always uses the equipped weapon.");
    recompinput::set_game_input_description(
        GameInput::OBJECTIVES,
        "Toggle a live objectives panel during gameplay without pausing.");

    twine::keyboard::configure_defaults();
    recompinput::set_default_mapping_for_controller(
        GameInput::SAVE_STATE, {InputField::controller_digital(SDL_CONTROLLER_BUTTON_PADDLE4)});
    recompinput::set_default_mapping_for_controller(
        GameInput::RESTORE_STATE, {InputField::controller_digital(SDL_CONTROLLER_BUTTON_PADDLE3)});

    recompinput::set_default_mapping_for_controller(
        GameInput::A,
        {InputField::controller_digital(
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)});
    recompinput::set_default_mapping_for_controller(
        GameInput::B,
        {InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_WEST)});
    recompinput::set_default_mapping_for_controller(
        GameInput::Z,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::L,
        {InputField::controller_analog(SDL_CONTROLLER_AXIS_TRIGGERLEFT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::R,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)});
    recompinput::set_default_mapping_for_controller(GameInput::RELOAD, {});
    recompinput::set_default_mapping_for_controller(
        GameInput::CYCLE_MODE,
        {InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_NORTH)});
    recompinput::set_default_mapping_for_controller(
        GameInput::ZOOM_IN,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_UP)});
    recompinput::set_default_mapping_for_controller(
        GameInput::ZOOM_OUT,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN)});
    recompinput::set_default_mapping_for_controller(
        GameInput::TOGGLE_XRAY,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_UP)});
    recompinput::set_default_mapping_for_controller(
        GameInput::WATCH_LASER,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_LEFT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::WATCH_GRAPPLE,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN)});
    recompinput::set_default_mapping_for_controller(
        GameInput::WATCH_DART,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::OBJECTIVES,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_BACK)});
    recompinput::set_default_mapping_for_controller(
        GameInput::TOGGLE_LIGHTING, {});
    recompinput::set_default_mapping_for_controller(
        GameInput::TOGGLE_MENU, {});
    recompinput::set_default_mapping_for_controller(
        GameInput::C_UP,
        {InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_SOUTH)});
    recompinput::set_default_mapping_for_controller(
        GameInput::C_DOWN,
        {InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_EAST)});
    recompinput::set_default_mapping_for_controller(
        GameInput::BACK_MENU,
        {InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_EAST)});
    recompinput::set_default_mapping_for_controller(GameInput::C_LEFT, {});
    recompinput::set_default_mapping_for_controller(GameInput::C_RIGHT, {});
    recompinput::set_default_mapping_for_controller(
        GameInput::DPAD_LEFT,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_LEFT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::DPAD_RIGHT,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)});
    recompinput::set_default_mapping_for_controller(
        GameInput::DPAD_UP,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_UP)});
    recompinput::set_default_mapping_for_controller(
        GameInput::DPAD_DOWN,
        {InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN)});
}

void migrate_default_bindings(
    const std::filesystem::path& config_path
) {
    using recompinput::GameInput;
    using recompinput::InputField;

    bool changed = false;
    const int controller_profile =
        recompinput::profiles::get_sp_controller_profile_index();
    const InputField old_default = InputField::controller_digital(
        recompinput::SDL_CONTROLLER_BUTTON_WEST);
    const InputField binding = recompinput::profiles::get_input_binding(
        controller_profile,
        GameInput::BACK_MENU,
        0);
    if (binding == old_default &&
        recompinput::profiles::get_input_binding(
            controller_profile,
            GameInput::BACK_MENU,
            1).is_empty()) {
        recompinput::profiles::set_input_binding(controller_profile, GameInput::BACK_MENU, 0,
            InputField::controller_digital(recompinput::SDL_CONTROLLER_BUTTON_EAST));
        changed = true;
    }

    const auto migrate_controller_default = [&](
        GameInput input,
        InputField from,
        InputField to
    ) {
        const InputField primary = recompinput::profiles::get_input_binding(
            controller_profile,
            input,
            0);
        if (primary == from &&
            recompinput::profiles::get_input_binding(
                controller_profile,
                input,
                1).is_empty()) {
            recompinput::profiles::set_input_binding(controller_profile, input, 0, to);
            changed = true;
        }
    };
    migrate_controller_default(
        GameInput::A,
        InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_NORTH),
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    migrate_controller_default(
        GameInput::R,
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    migrate_controller_default(
        GameInput::RELOAD,
        InputField::controller_digital(
            recompinput::SDL_CONTROLLER_BUTTON_WEST),
        {});

    for (size_t binding_index = 0;
         binding_index < recompinput::num_bindings_per_input;
         ++binding_index) {
        const InputField menu_binding = recompinput::profiles::get_input_binding(
            controller_profile,
            GameInput::TOGGLE_MENU,
            binding_index);
        if (!menu_binding.is_empty()) {
            recompinput::profiles::set_input_binding(controller_profile, GameInput::TOGGLE_MENU, binding_index, {});
            changed = true;
        }
    }
    migrate_controller_default(
        GameInput::TOGGLE_LIGHTING,
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_BACK),
        {});
    migrate_controller_default(
        GameInput::ZOOM_IN,
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK),
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_UP));
    migrate_controller_default(
        GameInput::ZOOM_OUT,
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_LEFTSTICK),
        InputField::controller_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN));

    changed |= twine::keyboard::migrate_defaults();

    if (changed &&
        !recompinput::profiles::save_controls_config(
            config_path / "controls.json")) {
        std::fprintf(stderr, "Failed to save corrected default bindings\n");
    }
}

RspUcodeFunc* rsp_microcode(const OSTask* task) {
    if (task != nullptr && task->t.type == M_AUDTASK) {
        return twine_audio;
    }

    return nullptr;
}

std::string game_thread_name(const OSThread* thread) {
    return thread == nullptr ? "TWINE" : "TWINE " + std::to_string(thread->id);
}

void on_vi() {

    const uint64_t frame = modern_vi_frame.fetch_add(
        1, std::memory_order_relaxed) + 1U;
    uint8_t* rdram = runtime_rdram.load(std::memory_order_acquire);
    bool mission_pause_present = false;
    if (rdram != nullptr && current_overlay(rdram) == 1 &&
            current_native_menu_page(rdram) >= 0) {
        const uint32_t manager = TWINE_MEM_W(0X9BB0, 0X80100000U);
        mission_pause_present = valid_rdram_address(manager, 0X34U) &&
            (static_cast<uint32_t>(TWINE_MEM_W(0XC, manager)) ==
                pause_menu_definition ||
             static_cast<uint32_t>(TWINE_MEM_W(0X10, manager)) ==
                pause_menu_definition);
    }
    twine::grapple::publish_world_active(
        rdram != nullptr && twine::render::mission_world_active(
            current_overlay(rdram), current_demo_mode(rdram),
            mission_pause_present));
    recompinput::update_rumble();

}

const char* rom_error(recomp::RomValidationError error) {
    switch (error) {
    case recomp::RomValidationError::Good:
        return "none";
    case recomp::RomValidationError::FailedToOpen:
        return "the ROM could not be opened";
    case recomp::RomValidationError::NotARom:
        return "the file is not an N64 ROM";
    case recomp::RomValidationError::IncorrectRom:
        return "the ROM is for a different game";
    case recomp::RomValidationError::IncorrectVersion:
        return "only the North American revision 0 ROM is supported";
    case recomp::RomValidationError::NotYet:
        return "this ROM revision is not supported yet";
    case recomp::RomValidationError::OtherError:
        return "the ROM could not be validated";
    case recomp::RomValidationError::FailedToStore:
        return "could not store the ROM; check configuration-folder permissions and free disk space";
    }
    return "the ROM could not be validated";
}

int run_application(int argc, char** argv) {
    const recomp::Version project_version{
        TWINE_VERSION_MAJOR,
        TWINE_VERSION_MINOR,
        TWINE_VERSION_PATCH,
    };
    const std::string display_version = project_version.to_string();

    recompui::programconfig::set_program_name(
        "TWINE Recompiled v" + display_version);
    recompui::programconfig::set_program_id(u8"TWINERecompiled");

    std::filesystem::path config_path =
        recompui::file::get_app_folder_path();
    std::error_code error;
    std::filesystem::create_directories(config_path, error);
    if (error) {
        std::fprintf(
            stderr,
            "Failed to create config directory: %s\n",
            error.message().c_str());
        const auto path_text = config_path.u8string();
        const std::string message = "Cannot create the game data directory:\n" +
            std::string(path_text.begin(), path_text.end()) + "\n\n" +
            error.message() + "\nMove the portable game to a writable folder.";
        recompui::file::show_error_message_box("Game data unavailable", message.c_str());
        return EXIT_FAILURE;
    }
    twine::fonts::initialize(config_path);
    twine::textures::initialize(config_path, std::u8string(game_id));
    twine::state::initialize(config_path);
    recomp::register_config_path(config_path);
    recomp::register_rom_search_path(recompui::file::get_install_path());
    twine::pfs_runtime::initialize(config_path);
    if (!twine::campaign::initialize(config_path)) {
        std::fprintf(
            stderr,
            "Campaign profile storage is unavailable; progress remains retryable for this session.\n");
    }

    constexpr const char* font = "LatoLatin-Regular.ttf";
    constexpr const char* bold_font = "LatoLatin-Bold.ttf";
    for (const auto* name : {font, bold_font}) {
        const auto path = recompui::file::get_asset_path(name);
        if (!std::filesystem::is_regular_file(path)) {
            std::fprintf(stderr, "Required UI font was not found: %s\n", path.string().c_str());
            return EXIT_FAILURE;
        }
    }
    recompui::register_primary_font(font, "LatoLatin");
    recompui::register_extra_font(bold_font);

    if (!twine::audio_host::initialize()) {
        std::fprintf(
            stderr,
            "Failed to open an audio device: %s\n",
            SDL_GetError());
        return EXIT_FAILURE;
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_mouse_sensitivity = true;
    general_options.extra_contents =
        [](recompui::ContextId context, recompui::Element* parent) {
            auto* reset = context.create_element<recompui::Button>(
                parent,
                "Clear Campaign Data",
                recompui::ButtonStyle::Danger);
            reset->set_margin_top(24.0f);
            reset->add_pressed_callback([]() {
                recompui::open_choice_prompt(
                    "Reset Campaign Progress?",
                    "This erases mission completion, scores, records, unlocks, and selected bonuses. Controls and display settings are kept.",
                    "Reset Campaign",
                    "Cancel",
                    []() {
                        if (twine::campaign::request_reset()) {
                            recompui::open_info_prompt(
                                "Campaign Reset",
                                "Campaign progress was reset. Return to the game to reload the fresh campaign.",
                                "OK",
                                {},
                                recompui::ButtonStyle::Tertiary);
                        }
                        else {
                            recompui::open_info_prompt(
                                "Campaign Not Reset",
                                "Start the game first, then retry. Existing progress was not changed.",
                                "OK",
                                {},
                                recompui::ButtonStyle::Danger);
                        }
                    },
                    []() {},
                    recompui::ButtonStyle::Danger,
                    recompui::ButtonStyle::Secondary,
                    true);
            });
        };
    recomp::config::Config& general_config =
        recompui::config::create_general_tab(general_options);
    general_config.add_bool_option(
        mouse_acceleration_option,
        "Mouse Acceleration",
        "Increases mouse-look speed with faster mouse movement.",
        false);
    general_config.add_number_option(joystick_sensitivity_x_option,
        "Right Stick Horizontal Sensitivity", "Horizontal speed for right-stick aiming.",
        0, 200, 5, 0, true, 100);
    general_config.add_number_option(joystick_sensitivity_y_option,
        "Right Stick Vertical Sensitivity", "Vertical speed for right-stick aiming.",
        0, 200, 5, 0, true, 100);
    general_config.add_bool_option(
        auto_aim_option,
        "Auto Aim",
        "Lets the game pull the reticle toward nearby enemies.",
        recompui::is_steam_deck());
    recomp::config::Config& gameplay_config =
        recompui::config::create_config_tab("QOL", twine::qol::config_id, false);
    twine::qol::register_settings(gameplay_config);
    recomp::config::Config& cheats_config =
        recompui::config::create_config_tab(
            "Cheats", twine::cheats::config_id, false);
    twine::cheats::register_settings(cheats_config);
    recompui::config::create_graphics_tab(recompui::config::graphics::tab_name,
        twine::textures::create_controls, twine::textures::select, twine::textures::available());
    configure_modern_controls();
    recompui::config::create_controls_tab();
    twine::audio_settings::create();
    recompui::config::finalize();
    twine::textures::configure();
    auto& finalized_gameplay_config =
        recompui::config::get_config(twine::qol::config_id);
    twine::qol::apply_default_migrations(finalized_gameplay_config);
    twine::cheats::migrate_fall_damage(
        recompui::config::get_config(twine::cheats::config_id),
        twine::qol::legacy_fall_damage_disabled());
    if (twine::qol::legacy_bonuses_enabled()) {
        auto& finalized_cheats_config =
            recompui::config::get_config(twine::cheats::config_id);
        if (twine::cheats::enable_all(finalized_cheats_config)) {
            finalized_gameplay_config.update_option_value(
                twine::qol::legacy_bonuses_key, uint32_t{0});
            const bool legacy_reset_saved =
                finalized_gameplay_config.save_config();

        }

    }
    migrate_default_bindings(config_path);

    recompinput::players::set_single_player_mode(true);
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        const auto& game = supported_games[0];
        auto* options = menu->init_game_options_menu(game.game_id, game.mod_game_id,
            game.display_name, game.thumbnail_bytes, recompui::GameOptionsMenuLayout::Center);
        options->add_start_game_or_load_rom_option("Load ROM", "Start Game", true);
        options->add_setup_controls_option();
        options->add_settings_option();
        options->add_mods_option();
        options->add_quit_option();
        auto selected_game = game.game_id;
        if (recomp::is_rom_valid(selected_game)) {
            recompui::queue_ui_action([options] { options->start_game(); });
        }
    });
    recompui::register_ui_exports();

    for (const recomp::GameEntry& game : supported_games) {
        if (!recomp::register_game(game)) {
            std::fprintf(stderr, "Failed to register the supported game\n");
            return EXIT_FAILURE;
        }
    }

    if (argc > 2) {
        std::fprintf(stderr, "Usage: %s [path-to-supported-rom]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        std::u8string selected_game(game_id);
        const recomp::RomValidationError result =
            recomp::select_rom(std::filesystem::path(argv[1]), selected_game);
        if (result != recomp::RomValidationError::Good) {
            std::fprintf(
                stderr,
                "ROM validation failed: %s\n",
                rom_error(result));
            return EXIT_FAILURE;
        }
    }

    const recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = rsp_microcode,
    };
    const ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context =
            [](uint8_t* rdram,
               ultramodern::renderer::WindowHandle handle,
               bool developer_mode) {
                return recompui::renderer::create_render_context(
                    rdram,
                    handle,
                    ultramodern::renderer::PresentationMode::PresentEarly,
                    developer_mode);
            },
    };
    const ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = twine::audio_host::queue_samples,
        .get_frames_remaining = twine::audio_host::frames_remaining,
        .set_frequency = twine::audio_host::set_frequency,
    };
    const ultramodern::input::callbacks_t input_callbacks{
        .poll_input = [] {
            recompinput::poll_inputs();
        },
        .get_input = get_input,
        .set_rumble = recompinput::set_rumble,
        .get_connected_device_info = connected_device,
    };
    const ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx = create_gfx,
        .create_window = create_window,
        .update_gfx = update_gfx,
        .shutdown_gfx_producers = twine::textures::shutdown,
    };

    const ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = on_vi,
        .gfx_init_callback = nullptr,
        .graphics_task_submitted_callback = [](uint64_t sequence, uint32_t displaylist) {
            RT64::submitModernRenderTask(sequence, displaylist);

        },
        .graphics_task_started_callback = [](uint64_t sequence, uint32_t displaylist) {
            RT64::beginModernRenderTask(sequence, displaylist);

        },
        .graphics_task_completed_callback = [](uint64_t sequence, uint32_t displaylist) {

            RT64::endModernRenderTask();
        },
    };
    const ultramodern::error_handling::callbacks_t error_callbacks{
        .message_box = recompui::message_box,
    };
    const ultramodern::threads::callbacks_t thread_callbacks{
        .get_game_thread_name = game_thread_name,
    };

    recomp::start(
        project_version,
        {},
        rsp_callbacks,
        renderer_callbacks,
        audio_callbacks,
        input_callbacks,
        gfx_callbacks,
        events_callbacks,
        error_callbacks,
        thread_callbacks);
    return EXIT_SUCCESS;
}

}

extern "C" uint32_t twine_override_modern_axis(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (current_demo_mode(rdram) != 0 ||
            !twine::modern_input::gameplay_overlay_active(
                current_overlay(rdram))) {
        return 0;
    }
    if (static_cast<uint64_t>(ctx->r4) >= modern_inputs.size()) {

        return 0;
    }

    const twine::modern_input::State input =
        modern_inputs[static_cast<size_t>(ctx->r4)].load();
    float value = 0.0f;
    if (!twine::modern_input::action_axis(
            static_cast<uint32_t>(ctx->r5),
            static_cast<uint32_t>(ctx->r6),
            input,
            value)) {

        return 0;
    }

    ctx->f0.fl = value;

    return 1;
}

extern "C" void twine_apply_modern_zoom(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (current_demo_mode(rdram) != 0 ||
            static_cast<uint64_t>(ctx->r19) >= held_modern_actions.size()) {
        return;
    }
    const uint8_t actions = held_modern_actions[ctx->r19].load(
        std::memory_order_relaxed);
    const float axis = twine::sniper::zoom_axis(
        (actions & twine::modern_input::action_bit(
            twine::modern_input::Action::ZoomIn)) != 0,
        (actions & twine::modern_input::action_bit(
            twine::modern_input::Action::ZoomOut)) != 0);
    if (axis != 0.0f) {
        ctx->f0.fl = axis;
    }
}

extern "C" void twine_match_portal_aspect(
    uint8_t*,
    recomp_context* ctx
) {
    const float scale = ultramodern::get_aspect_ratio_scale();
    ctx->f2.fl = twine::aspect::portal_projection_coefficient(
        ctx->f2.fl, scale);
}

extern "C" void twine_apply_gameplay_input_layout(
    uint8_t* rdram,
    recomp_context* ctx
) {
    const bool gameplay = gameplay_mapping_active(rdram);
    static bool overriding = false;
    if (gameplay) {
        ctx->r30 = 4;
        if (!overriding) {
            overriding = true;

        }
    }
    else if (overriding) {
        overriding = false;

    }
}

extern "C" void twine_track_attract_session(
    uint8_t* rdram,
    recomp_context*
) {
    const bool frontend = current_overlay(rdram) == 1;
    const bool demo = current_demo_mode(rdram) != 0;
    uint32_t active_mission = static_cast<uint32_t>(
        TWINE_MEM_W(-0XF8, 0X800C0000U));
    attract_session.observe(frontend, demo, active_mission);
    if (!frontend || demo || !attract_session.restore(active_mission)) {
        return;
    }
    TWINE_MEM_W(-0XF8, 0X800C0000U) = active_mission;

}

extern "C" void twine_begin_menu_transition(
    uint8_t* rdram,
    recomp_context* ctx
) {

    instant_pause_opening = current_overlay(rdram) > 1 &&
        current_demo_mode(rdram) == 0 &&
        (ctx->r4 & 0XFFFFU) == 1U &&
        static_cast<uint32_t>(ctx->r5) == 0X66FFFF00U &&
        (ctx->r6 & 0XFFFFU) == 13U;
}

extern "C" void twine_complete_instant_pause_movie(
    uint8_t*,
    recomp_context* ctx
) {
    if (complete_pause_movie) {

        ctx->r2 = 4;
    }
}

extern "C" void twine_finish_menu_transition(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (!instant_pause_opening) {
        return;
    }
    instant_pause_opening = false;
    const uint32_t menu = TWINE_MEM_W(0X2F2C, 0X80100000U);
    const uint32_t manager = TWINE_MEM_W(0X9BB0, 0X80100000U);
    if (!valid_rdram_address(menu, 0X80U) ||
            !valid_rdram_address(manager, 0X34U) ||
            TWINE_MEM_H(0X7A, menu) != 20) {
        return;
    }

    recomp_context fast = *ctx;
    TWINE_MEM_B(0X30, manager) = 0;
    func_800285D4(rdram, &fast);
    if (TWINE_MEM_H(0X7A, menu) != 21) {
        return;
    }
    TWINE_MEM_B(0X30, manager) = 0XFF;
    func_80028770(rdram, &fast);
    if (TWINE_MEM_H(0X7A, menu) != 13) {
        return;
    }
    complete_pause_movie = true;
    func_80028158(rdram, &fast);
    complete_pause_movie = false;

}

extern "C" void twine_complete_instant_pause_close_status(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t status
) {
    if (instant_pause_closing_requested.load(std::memory_order_acquire) &&
            current_overlay(rdram) > 1 && current_demo_mode(rdram) == 0 &&
            current_native_menu_page(rdram) == 14) {
        ctx->r2 = status;
    }
}

extern "C" void twine_finish_instant_pause_close(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (!instant_pause_closing_requested.load(std::memory_order_acquire) ||
            current_overlay(rdram) <= 1 || current_demo_mode(rdram) != 0 ||
            current_native_menu_page(rdram) != 15) {
        return;
    }
    recomp_context cleanup = *ctx;
    func_800283B0(rdram, &cleanup);
    instant_pause_closing_requested.store(false, std::memory_order_release);

}

extern "C" void twine_begin_ui_aspect(uint8_t* rdram, recomp_context* ctx) {
    const bool frontend = current_native_menu_page(rdram) >= 0;
    const bool mission = twine::hud::mission_ui_context(
        current_overlay(rdram), current_demo_mode(rdram), frontend);
    const bool expanded_hud = ultramodern::renderer::get_graphics_config().hr_option ==
        ultramodern::renderer::HUDRatioMode::Full;
    twine::render::ui::begin(rdram, uint32_t(ctx->r4), mission, frontend, expanded_hud);
}

extern "C" void twine_apply_modern_look(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (current_demo_mode(rdram) != 0 ||
            !twine::modern_input::gameplay_overlay_active(
                current_overlay(rdram))) {
        last_gameplay_input_frame.store(0, std::memory_order_relaxed);
        return;
    }
    if (static_cast<uint64_t>(ctx->r19) >= modern_inputs.size()) {
        return;
    }

    const size_t player = static_cast<size_t>(ctx->r19);
    gameplay_input_owner.observe(static_cast<unsigned>(player));
    if (twine::modern_input::native_ladder_look_owned(
            TWINE_MEM_HU(0x7C, ctx->r18), TWINE_MEM_BU(0x181, ctx->r17))) {

        modern_pitch_states[player].initialized = false;
        TWINE_MEM_W(0, 0x800E1A1C) = 0;
        return;
    }
    const twine::modern_input::State input = modern_inputs[player].load();
    fpr zoom{};
    zoom.u32l = static_cast<uint32_t>(TWINE_MEM_W(0X3C, ctx->r17));
    const float look_scale = twine::sniper::look_scale(zoom.fl);
    TWINE_MEM_B(0X5089 + player * 0XA0, 0X80110000U) =
        std::get<bool>(
            recompui::config::get_general_config().get_option_value(
                auto_aim_option)) ? 1 : 0;

    fpr yaw{};
    yaw.fl = twine::modern_input::yaw_delta(input.look_right * look_scale);
    TWINE_MEM_W(0X1A1C, 0X800E0000U) =
        static_cast<int32_t>(yaw.u32l);

    fpr pitch{};
    pitch.u32l = static_cast<uint32_t>(TWINE_MEM_W(0X3C, ctx->r18));
    const float native_pitch = pitch.fl;
    const uint32_t owner = static_cast<uint32_t>(ctx->r18);
    const uint64_t frame = modern_vi_frame.load(std::memory_order_relaxed);
    last_gameplay_input_frame.store(frame, std::memory_order_relaxed);
    pitch.fl = twine::modern_input::update_pitch(
        modern_pitch_states[player],
        owner,
        frame,
        native_pitch,
        input.look_up * look_scale);
    TWINE_MEM_W(0X2C, ctx->r17) = 0;
    TWINE_MEM_W(0X30, ctx->r17) = 0;
    TWINE_MEM_W(0X34, ctx->r17) = 0;
    TWINE_MEM_W(0X38, ctx->r17) = 0;
    TWINE_MEM_W(0X3C, ctx->r18) = static_cast<int32_t>(pitch.u32l);
}

extern "C" void twine_begin_gameplay_input_tick(uint8_t* rdram, recomp_context*) {
    gameplay_input_owner.begin(twine::qol::lifecycle_epoch());
    twine::sprint::begin_tick();
    twine::health::begin_tick(rdram);
}

extern "C" void twine_finish_gameplay_input_tick(uint8_t* rdram, recomp_context* ctx) {
    gameplay_input_owner.finish();
    const auto pause = pause_menu_context(rdram);

    twine::health::finish_tick(rdram, pause.session && !pause.menu_present);
    twine::objectives::publish(rdram, ctx, pause.session && !pause.menu_present);
    twine::vision_battery::publish(rdram, pause.session && !pause.menu_present);
}

extern "C" void twine_filter_sniper_idle_sway(
    uint8_t* rdram,
    recomp_context* ctx
) {
    const gpr item_state = TWINE_MEM_W(0X68, ctx->r17);
    if (item_state == 0) {
        return;
    }
    const uint8_t item = static_cast<uint8_t>(
        TWINE_MEM_B(0XE, item_state));
    const bool scoped = (TWINE_MEM_HU(0XC, item_state) & 1U) != 0;
    ctx->f20.fl = twine::sniper::idle_sway_amplitude(
        item, scoped, ctx->f20.fl);
}

extern "C" void twine_apply_sniper_scope_toggle(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (static_cast<uint64_t>(ctx->r19) >= sniper_scope_intents.size()) {
        return;
    }
    const uint32_t player_object = static_cast<uint32_t>(ctx->r18);
    const uint32_t item_state = twine::grapple::rdram_range_valid(
        player_object, 0x6CU) ? TWINE_MEM_W(0x68, player_object) : 0U;
    const uint8_t item = twine::grapple::rdram_range_valid(
        item_state, 0x10U) ? TWINE_MEM_BU(0x0E, item_state) : 0U;
    auto& intent = sniper_scope_intents[ctx->r19];
    const uint64_t epoch = twine::qol::inventory_epoch();
    const bool remember = twine::qol::settings().weapon_modes ==
        twine::qol::WeaponModeMemory::On;
    if (!intent.select(epoch, item, remember)) {

        return;
    }
    const bool cycle_mode = twine_take_modern_action(
            rdram,
            ctx,
            twine::modern_input::action_bit(
                twine::modern_input::Action::CycleMode)) != 0;
    const uint16_t before = TWINE_MEM_HU(0x0C, item_state);
    if (cycle_mode) {
        intent.toggle(before, remember);
    }
    const bool aim_held = (TWINE_MEM_BU(0x18U * 2U,
        0x80115094U + static_cast<uint32_t>(ctx->r19) * 0xA0U) & 4U) != 0;
    const uint16_t after = twine::sniper::apply_scope_latch(
        before, intent.owns_scope, intent.scoped, aim_held);
    TWINE_MEM_H(0x0C, item_state) = after;

}

extern "C" uint32_t twine_take_modern_action(
    uint8_t*,
    recomp_context* ctx,
    uint32_t action
) {
    if (static_cast<uint64_t>(ctx->r19) >= pending_modern_actions.size()) {
        return 0;
    }
    const uint8_t mask = static_cast<uint8_t>(action);
    const uint8_t pending = pending_modern_actions[ctx->r19].fetch_and(
        static_cast<uint8_t>(~mask),
        std::memory_order_relaxed);
    return (pending & mask) != 0;
}

extern "C" void twine_apply_weapon_category(
    uint8_t* rdram,
    recomp_context* ctx
) {
    const gpr inventory = ctx->r20;
    const uint32_t player = TWINE_MEM_BU(0X182, inventory);
    if (player >= pending_weapon_categories.size()) {
        return;
    }

    const auto category = static_cast<twine::modern_input::WeaponCategory>(
        pending_weapon_categories[player].exchange(
            static_cast<uint8_t>(
                twine::modern_input::WeaponCategory::None),
            std::memory_order_relaxed));
    if (category == twine::modern_input::WeaponCategory::None) {
        return;
    }

    const gpr hud = TWINE_MEM_W(0XA8, inventory);
    const int16_t transition = TWINE_MEM_H(0X7A, hud);
    constexpr uint16_t allowed_transitions =
        (1U << 0) | (1U << 2) | (1U << 4) | (1U << 8);
    if (transition >= 0 && transition < 12 &&
        (allowed_transitions & (1U << transition)) == 0) {
        return;
    }

    const uint64_t available_weapons =
        twine::radial::weapon_mask(static_cast<int>(player));

    const gpr weapon_state = TWINE_MEM_W(0X68, ctx->r17);
    const uint8_t current = TWINE_MEM_BU(0XE, weapon_state);
    TWINE_MEM_B(0XF, weapon_state) =
        twine::modern_input::next_category_weapon(
            category,
            current,
            available_weapons);
}

std::vector<recomp::GameEntry> supported_games = {
    {
        .rom_hash = 0x20AC5D898372B3C7ULL,
        .internal_name = "TWINE",
        .display_name = "The World Is Probably Enough",
        .game_id = std::u8string(game_id),
        .mod_game_id = {},
        .save_type = recomp::SaveType::None,
        .thumbnail_bytes = std::span<const char>{},
        .is_enabled = false,
        .has_compressed_code = false,
        .entrypoint_address = 0xFFFFFFFF80000400ULL,
        .entrypoint = recomp_entrypoint,
        .on_init_callback = twine_on_init,
        .rom_size = 33554432,
        .prepare_callback = twine::local_aot::prepare,
    },
};

int main(int argc, char** argv) {
    recompui::programconfig::set_program_id(u8"TWINERecompiled");

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_STEAMDECK", "1");
    SDL_SetHint("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    int result = EXIT_FAILURE;
    try {
        result = run_application(argc, argv);
    }
    catch (const std::exception& exception) {
        std::fprintf(stderr, "Fatal error: %s\n", exception.what());
    }

    twine::audio_host::shutdown();

    twine::campaign::shutdown();
    SDL_Quit();
    return result;
}

twine::state::Bytes twine::state::capture_input(uint8_t*) {
    Writer out; out.u32(2);
    const auto attract = attract_session.capture();
    out.fields(attract.mission, attract.observed, attract.active, attract.frontend);
    for (const auto& p : modern_pitch_states) out.fields(p.owner, p.last_frame, p.value, p.initialized);
    for (const auto& s : sniper_scope_intents) {
        out.fields(s.epoch == qol::inventory_epoch(), s.item, s.owns_scope, s.scoped,
            s.modes[0], s.modes[1]);
    }
    out.fields(instant_pause_opening, complete_pause_movie, instant_pause_closing_requested.load());
    out.blob(capture_radial());
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_input(std::span<const uint8_t> bytes) {
    Reader in(bytes); const auto version = in.u32();
    if (version != 1 && version != 2) throw std::runtime_error("Invalid input state schema");
    qol::AttractSession::Snapshot attract;
    in.fields(attract.mission, attract.observed, attract.active, attract.frontend);
    std::array<modern_input::PitchState, 4> pitch;
    std::array<sniper::ScopeIntent, 4> scopes;
    for (auto& p : pitch) {
        in.fields(p.owner, p.last_frame, p.value, p.initialized);
        if (!std::isfinite(p.value) || (p.owner && !guest_range(p.owner, 0x84)))
            throw std::runtime_error("Invalid saved camera input state");
    }
    for (auto& s : scopes) {
        s.epoch = in.scalar<bool>(); in.fields(s.item, s.owns_scope, s.scoped);
        if (s.item >= 59) throw std::runtime_error("Invalid saved scope item");
        if (version == 2) {
            in.fields(s.modes[0], s.modes[1]);
            if (s.modes[0] > 2 || s.modes[1] > 2)
                throw std::runtime_error("Invalid saved scope preference");
        } else {
            s.remember_current();
        }
    }
    bool opening, movie, closing; in.fields(opening, movie, closing);
    auto radial = prepare_radial(in.blob(256)); in.end();
    return prepared_owner([pitch, scopes, attract, opening, movie, closing, radial = std::move(radial)]() mutable noexcept {
        qol::notify_lifecycle(qol::LifecycleEvent::CheckpointRestore);
        const auto frame = modern_vi_frame.load();
        for (size_t i = 0; i < pitch.size(); ++i) {
            pitch[i].last_frame = frame; modern_pitch_states[i] = pitch[i];
            scopes[i].epoch = scopes[i].epoch ? qol::inventory_epoch() : 0;
            sniper_scope_intents[i] = scopes[i];
            modern_inputs[i].store({}); held_modern_actions[i].store(0); pending_modern_actions[i].store(0);
            pending_weapon_categories[i].store(0);
        }
        attract_session.restore_snapshot(attract);
        instant_pause_opening = opening; complete_pause_movie = movie;
        instant_pause_closing_requested.store(closing);
        gameplay_input_owner.begin(qol::lifecycle_epoch()); gameplay_input_owner.finish();
        last_gameplay_input_frame.store(0); deck_face_a_requested.store(false); deck_face_b_requested.store(false);
        radial->commit();
    });
}
