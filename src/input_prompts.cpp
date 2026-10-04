#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>

#include "SDL.h"
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "hud_layout.hpp"
#include "input_prompts.hpp"
#include "input_prompt_text.hpp"
#include "save_state_owner.hpp"
#include "librecomp/addresses.hpp"
#include "recompinput/input_state.h"
#include "recompinput/input_types.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "recompui/recompui.h"
#include "twine_recomp.h"
#include "ultramodern/config.hpp"

namespace {

using recompinput::GameInput;
using recompinput::InputType;

constexpr uint32_t kseg0 = 0x80000000U;
constexpr size_t max_nodes = 256;
constexpr size_t max_text = 512;
constexpr size_t max_cache_entries = 256;
constexpr size_t tracked_inputs =
    static_cast<size_t>(GameInput::COUNT);
constexpr size_t prompt_pool_size =
    (max_cache_entries + 1) * max_text;
constexpr size_t max_overlay_prompts = 64;

enum class PromptInput : uint8_t {
    none,
    a,
    b,
};

struct Binding {
    int32_t type{};
    int32_t id{};
    auto operator<=>(const Binding&) const = default;
};

struct BindingSnapshot {
    bool controller{};
    SDL_GameControllerType controller_type{SDL_CONTROLLER_TYPE_UNKNOWN};
    std::array<
        std::array<Binding, recompinput::num_bindings_per_input>,
        tracked_inputs> bindings{};
    auto operator<=>(const BindingSnapshot&) const = default;
};

struct CacheEntry {
    uint32_t source{};
    uint64_t label_generation{};
    uint16_t source_size{};
    uint16_t adapted_size{};
    uint32_t game_address{};
    std::array<char, max_text> source_bytes{};
};

struct Replacement {
    uint32_t node{};
    uint32_t original{};
    uint32_t temporary{};
    uint16_t original_size{};
    int16_t original_x{};
    int16_t temporary_x{};
};

struct AdaptedText {
    uint32_t address{};
    uint16_t size{};
};

using OverlayPrompt = twine::input_prompts::OverlayPromptIdentity;
using OverlaySnapshot =
    twine::input_prompts::OverlayFrame<max_overlay_prompts>;

struct PromptState {
    uint8_t* rdram{};
    uint32_t pool{};
    size_t cache_count{};
    size_t replacement_count{};
    uint64_t label_generation{1};
    bool has_snapshot{};
    bool reported_allocation_failure{};
    bool reported_capacity_failure{};
    bool reported_exception{};
    BindingSnapshot snapshot{};
    twine::input_prompts::Labels labels{};
    std::array<CacheEntry, max_cache_entries> cache{};
    std::array<Replacement, max_nodes * 2> replacements{};
};

PromptState state;
std::mutex overlay_mutex;
std::atomic<uint64_t> overlay_generation{1};
OverlaySnapshot published_overlay{};
OverlaySnapshot acknowledged_overlay{};
OverlaySnapshot building_overlay{};
OverlaySnapshot frame_acknowledged_overlay{};
bool overlay_frame_open = false;
bool reported_overlay_capacity = false;
recompui::ContextId overlay_context = recompui::ContextId::null();
std::array<recompui::Label*, max_overlay_prompts> overlay_labels{};
OverlaySnapshot displayed_overlay{};
int displayed_window_width = 0;
int displayed_window_height = 0;
twine::hud::Canvas displayed_canvas{};
bool overlay_visible = false;
bool overlay_suppressed_by_prompt = false;
uint64_t observed_overlay_generation = 0;

bool valid_address(uint32_t address, size_t size) {
    if (address < kseg0) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(address - kseg0);
    return size <= recomp::mem_size &&
        offset <= recomp::mem_size - size;
}

uint8_t read_u8(uint8_t* rdram, uint32_t address) {
    return rdram[(address - kseg0) ^ 3U];
}

uint16_t read_u16(uint8_t* rdram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, rdram + ((address - kseg0) ^ 2U), sizeof(value));
    return value;
}

uint32_t read_u32(uint8_t* rdram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, rdram + (address - kseg0), sizeof(value));
    return value;
}

float read_f32(uint8_t* rdram, uint32_t address) {
    return std::bit_cast<float>(read_u32(rdram, address));
}

void write_u8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address - kseg0) ^ 3U] = value;
}

void write_u16(uint8_t* rdram, uint32_t address, uint16_t value) {
    std::memcpy(
        rdram + ((address - kseg0) ^ 2U),
        &value,
        sizeof(value));
}

void write_u32(uint8_t* rdram, uint32_t address, uint32_t value) {
    std::memcpy(rdram + (address - kseg0), &value, sizeof(value));
}

bool read_string(
    uint8_t* rdram,
    uint32_t address,
    std::array<char, max_text>& output,
    size_t& length
) {
    if (!valid_address(address, 1)) {
        return false;
    }
    for (length = 0; length < output.size(); ++length) {
        if (!valid_address(address + static_cast<uint32_t>(length), 1)) {
            return false;
        }
        const uint8_t byte =
            read_u8(rdram, address + static_cast<uint32_t>(length));
        if (byte == 0) {
            return true;
        }
        output[length] = static_cast<char>(byte);
    }
    return false;
}

void write_string(
    uint8_t* rdram,
    uint32_t address,
    std::string_view text
) {
    for (size_t index = 0; index < text.size(); ++index) {
        write_u8(
            rdram,
            address + static_cast<uint32_t>(index),
            static_cast<uint8_t>(text[index]));
    }
    write_u8(
        rdram,
        address + static_cast<uint32_t>(text.size()),
        0);
}

bool is_playstation(SDL_GameControllerType type) {
    return type == SDL_CONTROLLER_TYPE_PS3 ||
        type == SDL_CONTROLLER_TYPE_PS4 ||
        type == SDL_CONTROLLER_TYPE_PS5;
}

bool is_nintendo(SDL_GameControllerType type) {
    return type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

std::string keyboard_label(int32_t id) {
    if (id < 0 || id >= SDL_NUM_SCANCODES) {
        return "Unknown";
    }
    switch (static_cast<SDL_Scancode>(id)) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return "Enter";
    case SDL_SCANCODE_ESCAPE:
        return "Esc";
    case SDL_SCANCODE_SPACE:
        return "Space";
    case SDL_SCANCODE_BACKSPACE:
        return "Backspace";
    case SDL_SCANCODE_LSHIFT:
        return "L Shift";
    case SDL_SCANCODE_RSHIFT:
        return "R Shift";
    case SDL_SCANCODE_LCTRL:
        return "L Ctrl";
    case SDL_SCANCODE_RCTRL:
        return "R Ctrl";
    case SDL_SCANCODE_LALT:
        return "L Alt";
    case SDL_SCANCODE_RALT:
        return "R Alt";
    default:
        break;
    }
    const char* name =
        SDL_GetScancodeName(static_cast<SDL_Scancode>(id));
    return name == nullptr || name[0] == '\0' ? "Unknown" : name;
}

std::string controller_button_label(
    int32_t id,
    SDL_GameControllerType type
) {
    if (id < 0 || id >= SDL_CONTROLLER_BUTTON_MAX) {
        return "Unknown";
    }
    const auto button = static_cast<SDL_GameControllerButton>(id);
    const bool playstation = is_playstation(type);
    const bool nintendo = is_nintendo(type);
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        return playstation ? "Cross" : nintendo ? "B" : "A";
    case SDL_CONTROLLER_BUTTON_B:
        return playstation ? "Circle" : nintendo ? "A" : "B";
    case SDL_CONTROLLER_BUTTON_X:
        return playstation ? "Square" : nintendo ? "Y" : "X";
    case SDL_CONTROLLER_BUTTON_Y:
        return playstation ? "Triangle" : nintendo ? "X" : "Y";
    case SDL_CONTROLLER_BUTTON_BACK:
        return playstation
            ? (type == SDL_CONTROLLER_TYPE_PS5 ? "Create" : "Share")
            : nintendo ? "Minus" : "View";
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return playstation ? "PS" : nintendo ? "Home" : "Guide";
    case SDL_CONTROLLER_BUTTON_START:
        return playstation ? "Options" : nintendo ? "Plus" : "Menu";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return "L3";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return "R3";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return playstation ? "L1" : nintendo ? "L" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return playstation ? "R1" : nintendo ? "R" : "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return "D-Pad Up";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return "D-Pad Down";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return "D-Pad Left";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return "D-Pad Right";
    case SDL_CONTROLLER_BUTTON_MISC1:
        return nintendo ? "Capture" : "Misc";
    case SDL_CONTROLLER_BUTTON_PADDLE1:
        return "R4";
    case SDL_CONTROLLER_BUTTON_PADDLE2:
        return "L4";
    case SDL_CONTROLLER_BUTTON_PADDLE3:
        return "R5";
    case SDL_CONTROLLER_BUTTON_PADDLE4:
        return "L5";
    case SDL_CONTROLLER_BUTTON_TOUCHPAD:
        return "Touchpad";
    default:
        return "Unknown";
    }
}

std::string controller_axis_label(
    int32_t id,
    SDL_GameControllerType type
) {
    const int64_t magnitude =
        id < 0 ? -static_cast<int64_t>(id) : id;
    if (magnitude < 1 || magnitude > SDL_CONTROLLER_AXIS_MAX) {
        return "Unknown";
    }
    const bool positive = id > 0;
    const bool playstation = is_playstation(type);
    const bool nintendo = is_nintendo(type);
    switch (static_cast<SDL_GameControllerAxis>(magnitude - 1)) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        return positive ? "LS Right" : "LS Left";
    case SDL_CONTROLLER_AXIS_LEFTY:
        return positive ? "LS Down" : "LS Up";
    case SDL_CONTROLLER_AXIS_RIGHTX:
        return positive ? "RS Right" : "RS Left";
    case SDL_CONTROLLER_AXIS_RIGHTY:
        return positive ? "RS Down" : "RS Up";
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        return playstation ? "L2" : nintendo ? "ZL" : "LT";
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        return playstation ? "R2" : nintendo ? "ZR" : "RT";
    default:
        return "Unknown";
    }
}

std::string field_label(
    const BindingSnapshot& snapshot,
    const Binding& binding
) {
    switch (static_cast<InputType>(binding.type)) {
    case InputType::None:
        return {};
    case InputType::Keyboard:
        return keyboard_label(binding.id);
    case InputType::Mouse:
        switch (binding.id) {
        case SDL_BUTTON_LEFT:
            return "Mouse 1";
        case SDL_BUTTON_MIDDLE:
            return "Mouse 3";
        case SDL_BUTTON_RIGHT:
            return "Mouse 2";
        case SDL_BUTTON_X1:
            return "Mouse 4";
        case SDL_BUTTON_X2:
            return "Mouse 5";
        default:
            return "Unknown";
        }
    case InputType::ControllerDigital:
        return controller_button_label(
            binding.id,
            snapshot.controller_type);
    case InputType::ControllerAnalog:
        return controller_axis_label(
            binding.id,
            snapshot.controller_type);
    }
    return "Unknown";
}

const Binding& primary_binding(
    const BindingSnapshot& snapshot,
    GameInput input
) {
    return snapshot.bindings[static_cast<size_t>(input)][0];
}

std::string input_label(
    const BindingSnapshot& snapshot,
    GameInput input
) {
    std::string result;
    for (const Binding& binding :
         snapshot.bindings[static_cast<size_t>(input)]) {
        const std::string label = field_label(snapshot, binding);
        if (label.empty() || label == result) {
            continue;
        }
        if (!result.empty()) {
            result += "/";
        }
        result += label;
    }
    return result.empty() ? "Unbound" : result;
}

bool is_keyboard_binding(
    const BindingSnapshot& snapshot,
    GameInput input,
    SDL_Scancode key
) {
    const Binding& binding = primary_binding(snapshot, input);
    return binding.type == static_cast<int32_t>(InputType::Keyboard) &&
        binding.id == static_cast<int32_t>(key);
}

bool is_controller_axis(
    const BindingSnapshot& snapshot,
    GameInput input,
    int32_t axis
) {
    const Binding& binding = primary_binding(snapshot, input);
    return binding.type ==
            static_cast<int32_t>(InputType::ControllerAnalog) &&
        binding.id == axis;
}

bool is_controller_button(
    const BindingSnapshot& snapshot,
    GameInput input,
    SDL_GameControllerButton button
) {
    const Binding& binding = primary_binding(snapshot, input);
    return binding.type ==
            static_cast<int32_t>(InputType::ControllerDigital) &&
        binding.id == static_cast<int32_t>(button);
}

std::string joined_primary_labels(
    const BindingSnapshot& snapshot,
    std::array<GameInput, 4> inputs
) {
    std::string result;
    std::array<std::string, 4> seen;
    size_t seen_count = 0;
    for (GameInput input : inputs) {
        const std::string label =
            field_label(snapshot, primary_binding(snapshot, input));
        if (label.empty() || label == "Unknown" ||
                std::find(
                    seen.begin(),
                    seen.begin() + static_cast<ptrdiff_t>(seen_count),
                    label) !=
                    seen.begin() + static_cast<ptrdiff_t>(seen_count)) {
            continue;
        }
        seen[seen_count++] = label;
        if (!result.empty()) {
            result += "/";
        }
        result += label;
    }
    return result.empty() ? "Unbound" : result;
}

std::string direction_group(
    const BindingSnapshot& snapshot,
    GameInput up,
    GameInput down,
    GameInput left,
    GameInput right
) {
    if (!snapshot.controller) {
        if (is_keyboard_binding(snapshot, up, SDL_SCANCODE_W) &&
                is_keyboard_binding(snapshot, down, SDL_SCANCODE_S) &&
                is_keyboard_binding(snapshot, left, SDL_SCANCODE_A) &&
                is_keyboard_binding(snapshot, right, SDL_SCANCODE_D)) {
            return "WASD";
        }
        if (is_keyboard_binding(snapshot, up, SDL_SCANCODE_I) &&
                is_keyboard_binding(snapshot, down, SDL_SCANCODE_K) &&
                is_keyboard_binding(snapshot, left, SDL_SCANCODE_J) &&
                is_keyboard_binding(snapshot, right, SDL_SCANCODE_L)) {
            return "IJKL";
        }
        if (is_keyboard_binding(snapshot, up, SDL_SCANCODE_UP) &&
                is_keyboard_binding(snapshot, down, SDL_SCANCODE_DOWN) &&
                is_keyboard_binding(snapshot, left, SDL_SCANCODE_LEFT) &&
                is_keyboard_binding(snapshot, right, SDL_SCANCODE_RIGHT)) {
            return "Arrow Keys";
        }
    }
    if (is_controller_axis(snapshot, up, -2) &&
            is_controller_axis(snapshot, down, 2) &&
            is_controller_axis(snapshot, left, -1) &&
            is_controller_axis(snapshot, right, 1)) {
        return "Left Stick";
    }
    if (is_controller_axis(snapshot, up, -4) &&
            is_controller_axis(snapshot, down, 4) &&
            is_controller_axis(snapshot, left, -3) &&
            is_controller_axis(snapshot, right, 3)) {
        return "Right Stick";
    }
    if (is_controller_button(
            snapshot, up, SDL_CONTROLLER_BUTTON_DPAD_UP) &&
            is_controller_button(
                snapshot, down, SDL_CONTROLLER_BUTTON_DPAD_DOWN) &&
            is_controller_button(
                snapshot, left, SDL_CONTROLLER_BUTTON_DPAD_LEFT) &&
            is_controller_button(
                snapshot, right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        return "D-Pad";
    }
    return joined_primary_labels(snapshot, {up, down, left, right});
}

std::string movement_axis_pair(
    const BindingSnapshot& snapshot,
    GameInput first,
    GameInput second,
    int32_t negative_axis,
    int32_t positive_axis,
    std::string_view standard_label
) {
    const Binding& first_binding = primary_binding(snapshot, first);
    const Binding& second_binding = primary_binding(snapshot, second);
    const int32_t analog = static_cast<int32_t>(InputType::ControllerAnalog);
    if (first_binding.type == analog && second_binding.type == analog &&
            ((first_binding.id == negative_axis &&
              second_binding.id == positive_axis) ||
             (first_binding.id == positive_axis &&
              second_binding.id == negative_axis))) {
        return std::string(standard_label);
    }
    return input_label(snapshot, first) + "/" +
        input_label(snapshot, second);
}

twine::input_prompts::Labels build_labels(
    const BindingSnapshot& snapshot
) {
    return {
        .a = input_label(snapshot, GameInput::A),
        .b = input_label(snapshot, GameInput::B),
        .accept = input_label(snapshot, GameInput::ACCEPT_MENU),
        .back = input_label(
            snapshot,
            snapshot.controller ? GameInput::BACK_MENU : GameInput::B),
        .z = input_label(snapshot, GameInput::Z),
        .l = input_label(snapshot, GameInput::L),
        .r = input_label(snapshot, GameInput::R),
        .toggle_xray = input_label(snapshot, GameInput::TOGGLE_XRAY),
        .start = input_label(snapshot, GameInput::START),
        .c_up = input_label(snapshot, GameInput::C_UP),
        .c_down = input_label(snapshot, GameInput::C_DOWN),
        .c_left = input_label(snapshot, GameInput::C_LEFT),
        .c_right = input_label(snapshot, GameInput::C_RIGHT),
        .dpad_up = input_label(snapshot, GameInput::DPAD_UP),
        .dpad_down = input_label(snapshot, GameInput::DPAD_DOWN),
        .dpad_left = input_label(snapshot, GameInput::DPAD_LEFT),
        .dpad_right = input_label(snapshot, GameInput::DPAD_RIGHT),
        .stick = direction_group(
            snapshot,
            GameInput::Y_AXIS_POS,
            GameInput::Y_AXIS_NEG,
            GameInput::X_AXIS_NEG,
            GameInput::X_AXIS_POS),
        .move_vertical = movement_axis_pair(
            snapshot,
            GameInput::Y_AXIS_POS,
            GameInput::Y_AXIS_NEG,
            -2,
            2,
            "Left Stick Up/Down"),
        .move_horizontal = movement_axis_pair(
            snapshot,
            GameInput::X_AXIS_NEG,
            GameInput::X_AXIS_POS,
            -1,
            1,
            "Left Stick Left/Right"),
        .c_buttons = direction_group(
            snapshot,
            GameInput::C_UP,
            GameInput::C_DOWN,
            GameInput::C_LEFT,
            GameInput::C_RIGHT),
        .dpad = direction_group(
            snapshot,
            GameInput::DPAD_UP,
            GameInput::DPAD_DOWN,
            GameInput::DPAD_LEFT,
            GameInput::DPAD_RIGHT),
    };
}

BindingSnapshot capture_bindings() {
    BindingSnapshot snapshot;
    {
        auto controller = recompinput::lock_controller(
            recompinput::last_input_was_controller()
                ? recompinput::get_last_active_controller_id() : -1, 0);
        snapshot.controller = controller.get() != nullptr;
        if (snapshot.controller) {
            snapshot.controller_type = SDL_GameControllerGetType(controller.get());
        }
    }

    const int profile = snapshot.controller
        ? recompinput::profiles::get_sp_controller_profile_index()
        : recompinput::profiles::get_sp_keyboard_profile_index();
    if (profile < 0 ||
            profile >= recompinput::profiles::get_input_profile_count()) {
        return snapshot;
    }
    for (size_t input_index = 0;
         input_index < tracked_inputs;
         ++input_index) {
        for (size_t binding_index = 0;
             binding_index < recompinput::num_bindings_per_input;
             ++binding_index) {
            const recompinput::InputField& field =
                recompinput::profiles::get_input_binding(
                    profile,
                    static_cast<GameInput>(input_index),
                    binding_index);
            snapshot.bindings[input_index][binding_index] = {
                static_cast<int32_t>(field.input_type),
                field.input_id,
            };
        }
    }
    return snapshot;
}

void update_labels() {
    BindingSnapshot current = capture_bindings();
    if (!state.has_snapshot || current != state.snapshot) {
        state.snapshot = current;
        state.labels = build_labels(state.snapshot);
        state.has_snapshot = true;

        if (++state.label_generation == 0) {
            state.label_generation = 1;
        }
    }
}

bool ensure_pool(uint8_t* rdram) {
    if (state.pool != 0) {
        return true;
    }
    uint8_t* memory =
        static_cast<uint8_t*>(recomp::alloc(rdram, prompt_pool_size));
    if (memory == nullptr) {
        if (!state.reported_allocation_failure) {
            std::fprintf(
                stderr,
                "Input prompts disabled: failed to allocate %zu bytes\n",
                prompt_pool_size);
            state.reported_allocation_failure = true;
        }
        return false;
    }
    const ptrdiff_t offset = memory - rdram;
    if (offset < 0 ||
            static_cast<uint64_t>(offset) >
                recomp::mem_size - prompt_pool_size) {
        if (!state.reported_allocation_failure) {
            std::fprintf(
                stderr,
                "Input prompts disabled: runtime returned an invalid "
                "RDRAM allocation\n");
            state.reported_allocation_failure = true;
        }
        return false;
    }
    state.pool = kseg0 + static_cast<uint32_t>(offset);
    std::memset(memory, 0, prompt_pool_size);
    return true;
}

Binding prompt_binding(PromptInput input) {
    GameInput game_input;
    switch (input) {
    case PromptInput::a:
        game_input = GameInput::ACCEPT_MENU;
        break;
    case PromptInput::b:
        game_input = state.snapshot.controller
            ? GameInput::BACK_MENU
            : GameInput::B;
        break;
    case PromptInput::none:
        return {};
    }
    return primary_binding(state.snapshot, game_input);
}

twine::input_prompts::OverlayOrigin overlay_origin(
    uint32_t node,
    int32_t x,
    uint16_t flags
) {
    using twine::input_prompts::OverlayOrigin;
    if (twine_ui_widescreen_alignment_enabled() == 0) {
        return OverlayOrigin::Center;
    }
    uint8_t* rdram = state.rdram;
    switch (twine::hud::node_origin(node, x, flags,
            [rdram](uint32_t address) {
                return static_cast<uint32_t>(TWINE_MEM_W(0, address));
            })) {
    case twine::hud::Origin::Left:
        return OverlayOrigin::Left;
    case twine::hud::Origin::Right:
        return OverlayOrigin::Right;
    case twine::hud::Origin::None:
        return OverlayOrigin::Center;
    }
    return OverlayOrigin::Center;
}

bool append_overlay_prompt(
    OverlaySnapshot& snapshot,
    const OverlayPrompt& prompt
) {
    if (!snapshot.upsert(prompt)) {
        if (!reported_overlay_capacity) {

            reported_overlay_capacity = true;
        }
        return false;
    }
    return true;
}

bool snapshot_contains_prompt(
    const OverlaySnapshot& snapshot,
    const OverlayPrompt& prompt
) {
    return snapshot.contains_renderable(prompt);
}

void publish_overlay_snapshot(const OverlaySnapshot& snapshot) {
    std::lock_guard lock(overlay_mutex);
    if (published_overlay != snapshot) {
        published_overlay = snapshot;
        overlay_generation.fetch_add(1, std::memory_order_release);
    }
}

bool publish_overlay_prompt(
    OverlaySnapshot& snapshot,
    const OverlaySnapshot& acknowledged,
    uint32_t key,
    PromptInput input,
    int32_t x,
    int32_t y,
    uint16_t flags,
    uint8_t alpha
) {
    const Binding binding = prompt_binding(input);
    if (binding.type == static_cast<int32_t>(InputType::None)) {
        return false;
    }
    const OverlayPrompt prompt{
        .key = key,
        .binding_type = binding.type,
        .binding_id = binding.id,
        .x = x,
        .y = y,
        .origin = overlay_origin(key, x, flags),
        .alpha = alpha,
    };
    if (!append_overlay_prompt(snapshot, prompt)) {
        return false;
    }
    return snapshot_contains_prompt(acknowledged, prompt);
}

void clear_overlay_prompts() {
    std::lock_guard lock(overlay_mutex);
    acknowledged_overlay = {};
    building_overlay = {};
    frame_acknowledged_overlay = {};
    overlay_frame_open = false;
    if (published_overlay.count != 0) {
        published_overlay = {};
        overlay_generation.fetch_add(1, std::memory_order_release);
    }
}

void ensure_prompt_state(uint8_t* rdram) {
    if (state.rdram == rdram) {
        return;
    }
    state = {};
    state.rdram = rdram;
    clear_overlay_prompts();
}

AdaptedText cached_text(
    uint8_t* rdram,
    uint32_t source,
    std::string_view source_text
) {
    if (!ensure_pool(rdram)) {
        return {
            source,
            static_cast<uint16_t>(source_text.size()),
        };
    }
    CacheEntry* entry = nullptr;
    for (size_t index = 0; index < state.cache_count; ++index) {
        CacheEntry& candidate = state.cache[index];
        if (candidate.source == source) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr) {
        if (state.cache_count >= state.cache.size()) {
            if (!state.reported_capacity_failure) {

                state.reported_capacity_failure = true;
            }
            return {
                source,
                static_cast<uint16_t>(source_text.size()),
            };
        }
        entry = &state.cache[state.cache_count];
        entry->source = source;
        entry->game_address =
            state.pool +
            static_cast<uint32_t>((state.cache_count + 1) * max_text);
        ++state.cache_count;
    }

    const bool source_changed =
        entry->source_size != source_text.size() ||
        std::memcmp(
            entry->source_bytes.data(),
            source_text.data(),
            source_text.size()) != 0;
    if (source_changed ||
            entry->label_generation != state.label_generation) {
        std::string adapted =
            twine::input_prompts::rewrite(source_text, state.labels);
        if (adapted.size() >= max_text) {
            if (!state.reported_capacity_failure) {
                std::fprintf(
                    stderr,
                    "Input prompts: adapted text exceeds %zu bytes; keeping "
                    "the original label\n",
                    max_text - 1);
                state.reported_capacity_failure = true;
            }
            return {
                source,
                static_cast<uint16_t>(source_text.size()),
            };
        }
        write_string(rdram, entry->game_address, adapted);
        entry->source_size =
            static_cast<uint16_t>(source_text.size());
        std::memcpy(
            entry->source_bytes.data(),
            source_text.data(),
            source_text.size());
        entry->adapted_size =
            static_cast<uint16_t>(adapted.size());
        entry->label_generation = state.label_generation;
    }
    return {
        entry->game_address,
        entry->adapted_size,
    };
}

void replace_node_text(
    uint8_t* rdram,
    uint32_t node,
    uint32_t temporary,
    uint16_t temporary_size,
    int16_t temporary_x
) {
    if (!valid_address(node, 0x2C) ||
            state.replacement_count >= state.replacements.size()) {
        return;
    }
    const uint32_t original = read_u32(rdram, node + 0x18);
    Replacement replacement{
        node,
        original,
        temporary,
        read_u16(rdram, node + 0x1C),
        static_cast<int16_t>(read_u16(rdram, node + 0x20)),
        temporary_x,
    };
    write_u32(rdram, node + 0x18, temporary);
    write_u16(rdram, node + 0x1C, temporary_size);
    write_u16(rdram, node + 0x20, static_cast<uint16_t>(temporary_x));
    state.replacements[state.replacement_count++] = replacement;
}

void restore_prompts(uint8_t* rdram) {
    while (state.replacement_count != 0) {
        const Replacement& replacement =
            state.replacements[--state.replacement_count];
        if (valid_address(replacement.node, 0x2C) &&
                read_u32(rdram, replacement.node + 0x18) ==
                    replacement.temporary) {
            write_u32(
                rdram,
                replacement.node + 0x18,
                replacement.original);
            write_u16(
                rdram,
                replacement.node + 0x1C,
                replacement.original_size);
            if (static_cast<int16_t>(read_u16(
                    rdram, replacement.node + 0x20)) ==
                    replacement.temporary_x) {
                write_u16(
                    rdram,
                    replacement.node + 0x20,
                    static_cast<uint16_t>(replacement.original_x));
            }
        }
    }
}

PromptInput action_binding(
    std::string_view text,
    bool restart_quit_pair
) {
    const auto action = twine::input_prompts::resolve_confirmation_action(
        twine::input_prompts::classify_action(text),
        restart_quit_pair);
    switch (action) {
    case twine::input_prompts::ActionBinding::back:
        return PromptInput::b;
    case twine::input_prompts::ActionBinding::accept:
        return PromptInput::a;
    case twine::input_prompts::ActionBinding::restart:
    case twine::input_prompts::ActionBinding::quit:
        return PromptInput::none;
    default:
        return PromptInput::none;
    }
}

bool contains_prompt_token(std::string_view text) {
    const auto& source = twine::rom::metadata().text;
    const std::array tokens{
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_1)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_2)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_3)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_4)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_5)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_6)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_7)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_8)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_9)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_10)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_11)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_12)]},
        std::string_view{source[static_cast<size_t>(twine::rom::Text::detector_13)]},
    };
    for (std::string_view token : tokens) {
        if (text.find(token) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

size_t collect_nodes(
    uint8_t* rdram,
    uint32_t root,
    std::array<twine::input_prompts::PromptNode, max_nodes>& nodes
) {
    if (!valid_address(root, 0x68)) {
        return 0;
    }
    uint32_t node = read_u32(rdram, root + 4);
    size_t count = 0;
    while (count < nodes.size() && valid_address(node, 0x2C)) {
        nodes[count++] = {
            .address = node,
            .text = read_u32(rdram, node + 0x18),
            .x = static_cast<int16_t>(read_u16(rdram, node + 0x20)),
            .y = static_cast<int16_t>(read_u16(rdram, node + 0x22)),
            .flags = read_u16(rdram, node + 0x1E),
            .layer = static_cast<uint8_t>(
                (read_u32(rdram, node + 0x28) >> 2) & 0x3F),
            .alpha = read_u8(rdram, node + 0x13),
        };
        const uint32_t next = read_u32(rdram, node + 4);
        if (next == node) {
            break;
        }
        node = next;
    }
    return count;
}

int32_t root_coordinate(
    uint8_t* rdram,
    uint32_t root,
    uint32_t first_offset,
    uint32_t second_offset
) {
    if (!valid_address(root, 0x68)) {
        return 0;
    }
    const float value = read_f32(rdram, root + first_offset) +
        read_f32(rdram, root + second_offset);
    if (!std::isfinite(value) || value < -4096.0f || value > 4096.0f) {
        return 0;
    }
    return static_cast<int32_t>(std::trunc(value));
}

void prepare_prompts(uint8_t* rdram, recomp_context* ctx) {
    ensure_prompt_state(rdram);
    restore_prompts(rdram);
    update_labels();

    std::array<twine::input_prompts::PromptNode, max_nodes> nodes;
    std::array<bool, max_nodes> used_icons{};
    const uint32_t root = static_cast<uint32_t>(ctx->r4);
    const int32_t active_layer = static_cast<int32_t>(ctx->r6 & 0x3FU);

    OverlaySnapshot pending_overlay = overlay_frame_open
        ? building_overlay : OverlaySnapshot{};
    const int32_t root_x = root_coordinate(rdram, root, 0x50, 0x60);
    const int32_t root_y = root_coordinate(rdram, root, 0x58, 0x64);
    const size_t node_count =
        collect_nodes(rdram, root, nodes);
    std::array<char, max_text> source_buffer;
    std::array<bool, 64> layer_has_restart{};
    std::array<bool, 64> layer_has_quit{};
    std::array<bool, 64> layer_has_yes{};
    std::array<bool, 64> layer_has_no{};
    for (size_t index = 0; index < node_count; ++index) {
        if (nodes[index].text == 0 || nodes[index].alpha == 0) {
            continue;
        }
        size_t source_size;
        if (!read_string(
                rdram,
                nodes[index].text,
                source_buffer,
                source_size)) {
            continue;
        }
        const auto action = twine::input_prompts::classify_action(
            std::string_view(source_buffer.data(), source_size));
        const size_t layer = nodes[index].layer;
        layer_has_restart[layer] = layer_has_restart[layer] ||
            action == twine::input_prompts::ActionBinding::restart;
        layer_has_quit[layer] = layer_has_quit[layer] ||
            action == twine::input_prompts::ActionBinding::quit;
        const std::string_view source(source_buffer.data(), source_size);
        layer_has_yes[layer] = layer_has_yes[layer] ||
            twine::input_prompts::trimmed_equals(source, "Yes");
        layer_has_no[layer] = layer_has_no[layer] ||
            twine::input_prompts::trimmed_equals(source, "No");
    }
    int32_t confirmation_layer = -1;
    for (size_t layer = 0; layer < layer_has_yes.size(); ++layer) {
        if (layer_has_yes[layer] && layer_has_no[layer]) {
            confirmation_layer = static_cast<int32_t>(layer);
            break;
        }
    }

    for (size_t index = 0; index < node_count; ++index) {
        if (nodes[index].text == 0 || nodes[index].alpha == 0 ||
                nodes[index].layer != static_cast<uint8_t>(active_layer)) {
            continue;
        }
        size_t source_size;
        if (!read_string(
                rdram,
                nodes[index].text,
                source_buffer,
                source_size)) {
            continue;
        }
        const std::string_view source(
            source_buffer.data(),
            source_size);
        const bool restart_quit_pair =
            layer_has_restart[nodes[index].layer] &&
            layer_has_quit[nodes[index].layer];
        const PromptInput action = action_binding(source, restart_quit_pair);
        if (!twine::input_prompts::action_visible_for_modal(
                nodes[index].layer, confirmation_layer)) {
            continue;
        }
        if (action != PromptInput::none) {

            const size_t icon = twine::input_prompts::find_icon(
                std::span(nodes.data(), node_count),
                index,
                std::span(used_icons.data(), node_count));

            if (icon != node_count && ensure_pool(rdram) &&
                    publish_overlay_prompt(
                    pending_overlay,
                    frame_acknowledged_overlay,
                    nodes[icon].address,
                    action,
                    root_x + nodes[icon].x,
                    root_y + nodes[icon].y,
                    nodes[icon].flags,
                    std::min(nodes[index].alpha, nodes[icon].alpha))) {
                replace_node_text(
                    rdram,
                    nodes[icon].address,
                    state.pool,
                    0,
                    nodes[icon].x);
                used_icons[icon] = true;

            }
        }
        const bool has_prompt_token = contains_prompt_token(source);
        const AdaptedText adapted = has_prompt_token
            ? cached_text(rdram, nodes[index].text, source)
            : AdaptedText{
                nodes[index].text,
                static_cast<uint16_t>(source.size()),
            };
        if (has_prompt_token && adapted.address != nodes[index].text) {
            const size_t icon = twine::input_prompts::find_icon(
                std::span(nodes.data(), node_count),
                index,
                std::span(used_icons.data(), node_count));
            if (icon != node_count && ensure_pool(rdram)) {
                replace_node_text(
                    rdram,
                    nodes[icon].address,
                    state.pool,
                    0,
                    nodes[icon].x);
                used_icons[icon] = true;
            }
            replace_node_text(
                rdram,
                nodes[index].address,
                adapted.address,
                adapted.size,
                nodes[index].x);
        }
    }

    building_overlay = pending_overlay;
}

void begin_overlay_frame() {
    std::lock_guard lock(overlay_mutex);
    building_overlay = {};
    frame_acknowledged_overlay = acknowledged_overlay;
    overlay_frame_open = true;
}

void finish_overlay_frame() {
    if (!overlay_frame_open) {
        return;
    }
    overlay_frame_open = false;
    publish_overlay_snapshot(building_overlay);
}

}

namespace twine::input_prompts {

void initialize_overlay_ui() {
    if (overlay_context != recompui::ContextId::null()) {
        return;
    }
    overlay_context = recompui::create_context();
    overlay_context.open();
    overlay_context.set_captures_input(false);
    overlay_context.set_captures_mouse(false);
    auto* document = overlay_context.get_root_element();
    document->set_background_color(recompui::theme::color::Transparent);
    document->set_overflow(recompui::Overflow::Hidden);
    document->set_pointer_events(recompui::PointerEvents::None);
    auto* root = overlay_context.create_element<recompui::Element>(
        document);
    root->set_position(recompui::Position::Absolute);
    root->set_left(0.0f, recompui::Unit::Px);
    root->set_top(0.0f, recompui::Unit::Px);
    root->set_right(0.0f, recompui::Unit::Px);
    root->set_bottom(0.0f, recompui::Unit::Px);
    root->set_background_color(recompui::theme::color::Transparent);
    root->set_overflow(recompui::Overflow::Hidden);
    root->set_pointer_events(recompui::PointerEvents::None);
    for (recompui::Label*& label : overlay_labels) {
        label = overlay_context.create_element<recompui::Label>(
            root, "", recompui::LabelStyle::Small);
        label->set_position(recompui::Position::Absolute);
        label->set_font_family("promptfont");
        label->set_font_style(recompui::FontStyle::Normal);
        label->set_font_weight(400);
        label->set_text_align(recompui::TextAlign::Center);
        label->set_pointer_events(recompui::PointerEvents::None);
        label->set_color(recompui::theme::color::Text);
        label->set_display(recompui::Display::None);
    }
    overlay_context.close();
}

void update_ui() {
    if (recompui::is_prompt_open()) {
        if (overlay_visible) {
            recompui::hide_context(overlay_context);
            overlay_visible = false;
        }
        overlay_suppressed_by_prompt = true;
        return;
    }
    if (overlay_suppressed_by_prompt) {
        overlay_suppressed_by_prompt = false;
        observed_overlay_generation = 0;
    }
    OverlaySnapshot snapshot;
    const uint64_t generation =
        overlay_generation.load(std::memory_order_acquire);
    if (generation == observed_overlay_generation) {
        return;
    }
    {
        std::lock_guard lock(overlay_mutex);
        snapshot = published_overlay;
        observed_overlay_generation =
            overlay_generation.load(std::memory_order_acquire);
    }

    for (size_t index = 1; index < snapshot.count; ++index) {
        const OverlayPrompt prompt = snapshot.prompts[index];
        size_t destination = index;
        while (destination != 0 &&
                prompt.key < snapshot.prompts[destination - 1].key) {
            snapshot.prompts[destination] =
                snapshot.prompts[destination - 1];
            --destination;
        }
        snapshot.prompts[destination] = prompt;
    }

    if (snapshot.count == 0) {
        if (overlay_visible) {
            recompui::hide_context(overlay_context);
            overlay_visible = false;
        }
        displayed_overlay = {};
        {
            std::lock_guard lock(overlay_mutex);
            acknowledged_overlay = {};
        }
        return;
    }

    int window_width = 0;
    int window_height = 0;
    recompui::get_window_size(window_width, window_height);
    if (window_width <= 0 || window_height <= 0) {
        return;
    }
    initialize_overlay_ui();
    const auto graphics = ultramodern::renderer::get_graphics_config();
    const auto aspect = graphics.ar_option == ultramodern::renderer::AspectRatio::Original
        ? hud::Aspect::Original : graphics.ar_option == ultramodern::renderer::AspectRatio::Manual
        ? hud::Aspect::Wide : hud::Aspect::Expand;
    const auto safe = graphics.hr_option == ultramodern::renderer::HUDRatioMode::Original
        ? hud::SafeArea::Original : graphics.hr_option == ultramodern::renderer::HUDRatioMode::Clamp16x9
        ? hud::SafeArea::Clamp16x9 : hud::SafeArea::Full;
    const auto canvas = hud::canvas_for(window_width, window_height, aspect, safe);
    if (!overlay_visible) {
        recompui::show_context(overlay_context, {});
        overlay_visible = true;

    }

    if (snapshot == displayed_overlay &&
            window_width == displayed_window_width &&
            window_height == displayed_window_height && canvas == displayed_canvas) {
        {
            std::lock_guard lock(overlay_mutex);
            acknowledged_overlay = snapshot;
        }
        return;
    }

    overlay_context.open();
    for (size_t index = 0; index < overlay_labels.size(); ++index) {
        recompui::Label* label = overlay_labels[index];
        if (index >= snapshot.count) {
            label->set_display(recompui::Display::None);
            continue;
        }
        const OverlayPrompt& prompt = snapshot.prompts[index];
        const OverlayLayout layout = overlay_layout(
            window_width,
            window_height,
            prompt.x,
            prompt.y,
            prompt.origin, aspect, safe);
        const recompinput::InputField field{
            static_cast<InputType>(prompt.binding_type),
            prompt.binding_id,
        };
        const std::string glyph = field.to_string();
        label->set_text(glyph);
        label->set_left(layout.x, recompui::Unit::Px);
        label->set_top(layout.y, recompui::Unit::Px);
        label->set_width(layout.size, recompui::Unit::Px);
        label->set_height(layout.size, recompui::Unit::Px);
        label->set_font_family("promptfont");
        label->set_font_size(layout.size, recompui::Unit::Px);
        label->set_line_height(layout.size, recompui::Unit::Px);
        label->set_text_align(recompui::TextAlign::Center);
        label->set_white_space(recompui::WhiteSpace::Nowrap);
        label->set_color(recompui::theme::color::Text);
        label->set_opacity(static_cast<float>(prompt.alpha) / 255.0f);
        label->set_display(recompui::Display::Block);
    }
    overlay_context.close();
    {
        std::lock_guard lock(overlay_mutex);
        acknowledged_overlay = snapshot;
    }
    displayed_overlay = snapshot;
    displayed_window_width = window_width;
    displayed_window_height = window_height;
    displayed_canvas = canvas;
}

}

extern "C" void twine_reset_input_prompts(
    uint8_t* rdram,
    recomp_context*
) {
    state = {};
    state.rdram = rdram;

    ensure_pool(rdram);
    clear_overlay_prompts();
}

extern "C" void twine_prepare_input_prompts(
    uint8_t* rdram,
    recomp_context* ctx
) {
    try {
        prepare_prompts(rdram, ctx);
    }
    catch (const std::exception& exception) {
        restore_prompts(rdram);
        if (!state.reported_exception) {
            std::fprintf(
                stderr,
                "Input prompts disabled after an unexpected error: %s\n",
                exception.what());
            state.reported_exception = true;
        }
    }
    catch (...) {
        restore_prompts(rdram);
        if (!state.reported_exception) {
            std::fprintf(
                stderr,
                "Input prompts disabled after an unknown error\n");
            state.reported_exception = true;
        }
    }
}

extern "C" void twine_begin_input_prompt_frame(
    uint8_t* rdram,
    recomp_context*
) {
    if (state.rdram != rdram) {
        state = {};
        state.rdram = rdram;
        clear_overlay_prompts();
    }
    begin_overlay_frame();
}

extern "C" void twine_finish_input_prompt_frame(
    uint8_t*,
    recomp_context*
) {
    finish_overlay_frame();
}

extern "C" void twine_restore_input_prompts(
    uint8_t* rdram,
    recomp_context*
) {
    restore_prompts(rdram);
}

extern "C" void twine_prepare_localized_input_prompt(
    uint8_t* rdram,
    recomp_context* ctx
) {
    std::array<char, max_text> text{};
    size_t length = 0;
    const uint32_t source = static_cast<uint32_t>(ctx->r2);
    if (!read_string(rdram, source, text, length)) {
        return;
    }
    const std::string_view source_text{text.data(), length};
    if (!twine::input_prompts::is_contextual_tutorial(source_text)) {
        return;
    }
    ensure_prompt_state(rdram);
    update_labels();
    const AdaptedText adapted = cached_text(rdram, source, source_text);
    ctx->r2 = twine_n64_address(adapted.address);
}

std::pair<uint32_t, uint32_t> twine::state::prompt_heap_owner() {
    return {::state.pool, ::state.pool ? uint32_t(prompt_pool_size) : 0U};
}

twine::state::Bytes twine::state::capture_prompts(uint8_t* rdram) {
    if (::state.rdram != rdram || ::state.replacement_count || overlay_frame_open)
        throw std::runtime_error("Input prompts are not at their completed frame boundary");
    Writer out;
    out.fields(uint32_t(1), ::state.pool, uint32_t(::state.cache_count));
    if (::state.pool) out.blob({rdram + (::state.pool - kseg0), prompt_pool_size});
    else out.blob({});
    for (size_t i = 0; i < ::state.cache_count; ++i) {
        const auto& entry = ::state.cache[i];
        out.fields(entry.source, entry.source_size, entry.adapted_size, entry.game_address);
        out.blob({reinterpret_cast<const uint8_t*>(entry.source_bytes.data()), entry.source_size});
    }
    return std::move(out.bytes);
}

std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_prompts(std::span<const uint8_t> bytes) {
    struct PromptRestore final : PreparedOwner {
        Bytes pool;
        std::array<CacheEntry, max_cache_entries> entries{};
        uint32_t address = 0, count = 0;
        void commit() noexcept override {
            if (address) std::memcpy(::state.rdram + address - kseg0, pool.data(), pool.size());
            ::state.cache = entries;
            ::state.cache_count = count;
            ::state.has_snapshot = false;
            ::state.replacement_count = 0;
            clear_overlay_prompts();
        }
    };
    Reader in(bytes);
    if (in.u32() != 1) throw std::runtime_error("Invalid prompt state schema");
    auto prepared = std::make_unique<PromptRestore>();
    in.fields(prepared->address, prepared->count);
    const auto pool = in.blob(prompt_pool_size);
    if (::state.replacement_count || overlay_frame_open || prepared->address != ::state.pool ||
        prepared->count > max_cache_entries ||
        pool.size() != (prepared->address ? prompt_pool_size : 0) || (!prepared->address && prepared->count))
        throw std::runtime_error("Saved prompt pool differs from this process");
    prepared->pool.assign(pool.begin(), pool.end());
    for (uint32_t i = 0; i < prepared->count; ++i) {
        auto& entry = prepared->entries[i];
        in.fields(entry.source, entry.source_size, entry.adapted_size, entry.game_address);
        const auto source = in.blob(max_text - 1);
        const bool source_owned = guest_range(entry.source, entry.source_size + 1) ||
            (entry.source >= prepared->address && uint64_t(entry.source) + entry.source_size + 1 <=
                uint64_t(prepared->address) + prompt_pool_size);
        if (!source_owned || entry.source_size != source.size() || entry.adapted_size >= max_text ||
            entry.game_address != prepared->address + (i + 1) * max_text ||
            prepared->pool[((i + 1) * max_text + entry.adapted_size) ^ 3U] != 0)
            throw std::runtime_error("Invalid saved prompt cache entry");
        std::memcpy(entry.source_bytes.data(), source.data(), source.size());
    }
    in.end();
    return prepared;
}
