#include "save_state_shortcuts.hpp"
#include "save_state_service.hpp"
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"
#include "recompui/recompui.h"
#include "elements/ui_label.h"
#include "ultramodern/ultramodern.hpp"
#include "SDL.h"
#include <chrono>

void twine::state::update_shortcuts(const deck_grips::Edges& deck) {
    struct StateInput { bool held = false, pressed = false; };
    const auto state_binding = [&](recompinput::GameInput input) {
        StateInput result;
        for (auto device : {recompinput::InputDevice::Controller, recompinput::InputDevice::Keyboard}) {
            const int profile = recompinput::profiles::get_input_profile_for_player(0, device);
            if (profile < 0 || profile >= recompinput::profiles::get_input_profile_count()) continue;
            for (size_t i = 0; i < recompinput::num_bindings_per_input; ++i) {
                const auto binding = recompinput::profiles::get_input_binding(profile, input, i);

                if (deck.lower_available && binding.input_type == recompinput::InputType::ControllerDigital) {
                    if (binding.input_id == SDL_CONTROLLER_BUTTON_PADDLE4) {
                        result.held |= deck.l5_held; result.pressed |= deck.l5;
                        continue;
                    }
                    if (binding.input_id == SDL_CONTROLLER_BUTTON_PADDLE3) {
                        result.held |= deck.r5_held; result.pressed |= deck.r5;
                        continue;
                    }
                }
                result.held |= recompinput::get_input_digital(0, binding);
            }
        }
        return result;
    };
    static bool save_held = false, restore_held = false;
    const auto save_now = state_binding(recompinput::GameInput::SAVE_STATE);
    const auto restore_now = state_binding(recompinput::GameInput::RESTORE_STATE);
    const bool save_edge = save_now.pressed || (save_now.held && !save_held);
    const bool restore_edge = restore_now.pressed || (restore_now.held && !restore_held);
    save_held = save_now.held; restore_held = restore_now.held;
    if (!recompinput::game_input_disabled() && ultramodern::is_game_started()) {

        if (restore_edge) twine::state::restore();
        else if (save_edge) twine::state::save();
    }

    static auto state_context = recompui::ContextId::null();
    static recompui::Label* state_label = nullptr;
    static std::chrono::steady_clock::time_point state_notice_until{};
    std::string state_title, state_message;
    if (twine::state::take_notification(state_title, state_message)) {
        auto previous = recompui::try_close_current_context();
        if (state_context == recompui::ContextId::null()) {
            state_context = recompui::create_context();
            state_context.set_captures_input(false);
            state_context.set_captures_mouse(false);
            state_context.open();
            state_label = state_context.create_element<recompui::Label>(
                state_context.get_root_element(), "", recompui::LabelStyle::Small);
            state_label->set_position(recompui::Position::Absolute);
            state_label->set_left(24, recompui::Unit::Dp);
            state_label->set_bottom(24, recompui::Unit::Dp);
            state_label->set_max_width(600, recompui::Unit::Dp);
            state_label->set_padding(16, recompui::Unit::Dp);
            state_label->set_background_color(recompui::theme::color::BGOverlay);
        } else state_context.open();
        state_label->set_text(state_title + ": " + state_message);
        state_context.close();
        if (previous != recompui::ContextId::null()) previous.open();
        if (!recompui::is_context_shown(state_context)) recompui::show_context(state_context, "");
        state_notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
    if (state_context != recompui::ContextId::null() &&
        std::chrono::steady_clock::now() >= state_notice_until && recompui::is_context_shown(state_context))
        recompui::hide_context(state_context);
}
