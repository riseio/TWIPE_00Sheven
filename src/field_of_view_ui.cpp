#include "field_of_view.hpp"
#include "recompui/config.h"
#include "elements/ui_button.h"

namespace twine::fov {
void create_controls(recompui::ContextId context, recompui::Element* parent) {
    auto* reset = context.create_element<recompui::Button>(parent,
        "Reset Field of View to Default (60 degrees)", recompui::ButtonStyle::Secondary);
    reset->set_margin_top(24.0f);
    reset->add_pressed_callback([] {
        reset_default(recompui::config::get_config(recompui::config::graphics::id));
    });
}
}
