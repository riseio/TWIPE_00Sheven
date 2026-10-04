#include "audio_status_ui.hpp"
#include "audio_host.hpp"
#include "recompui/recompui.h"
#include "elements/ui_label.h"
#include "ultramodern/ultramodern.hpp"

namespace {
void show_status(twine::audio_host::DeviceStatus status) {
    using twine::audio_host::DeviceStatus;
    static auto context = recompui::ContextId::null();
    if (status != DeviceStatus::Unavailable) {
        if (context != recompui::ContextId::null() && recompui::is_context_shown(context))
            recompui::hide_context(context);
        return;
    }
    if (context == recompui::ContextId::null()) {
        auto previous = recompui::try_close_current_context();
        context = recompui::create_context();
        context.set_captures_input(false);
        context.set_captures_mouse(false);
        context.open();
        context.get_root_element()->set_background_color(recompui::theme::color::Transparent);
        context.get_root_element()->set_pointer_events(recompui::PointerEvents::None);
        auto* label = context.create_element<recompui::Label>(context.get_root_element(),
            "Audio unavailable. Sound will reconnect when an output device is available.",
            recompui::LabelStyle::Small);
        label->set_position(recompui::Position::Absolute);
        label->set_left(24, recompui::Unit::Dp);
        label->set_top(24, recompui::Unit::Dp);
        label->set_max_width(600, recompui::Unit::Dp);
        label->set_padding(12, recompui::Unit::Dp);
        label->set_background_color(recompui::theme::color::BGOverlay);
        context.close();
        if (previous != recompui::ContextId::null()) previous.open();
    }
    if (!recompui::is_context_shown(context)) recompui::show_context(context, "");
}
}

void twine::audio_status::update(bool resumed) {
    static auto previous_status = audio_host::DeviceStatus::Stopped;
    static bool previous_game_active = false;
    const auto status = audio_host::service_device_changes(resumed);
    const bool active = ultramodern::is_game_active();
    if (status == previous_status && active == previous_game_active) return;
    previous_status = status;
    previous_game_active = active;

    recompui::queue_ui_action([status] { show_status(status); });
}
