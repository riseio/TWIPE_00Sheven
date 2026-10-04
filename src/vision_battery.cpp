#include "vision_battery.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "recompui/recompui.h"
#include "recompinput/input_state.h"
#include "twine_qol.hpp"

namespace {
std::atomic<uint64_t> published{0};
recompui::ContextId context = recompui::ContextId::null();
recompui::Element* panel = nullptr;
recompui::Label* label = nullptr;
std::array<recompui::Element*, 10> segments{};
bool visible = false;
uint64_t previous = 0;
int previous_height = 0;

void initialize_ui() {
    if (context != recompui::ContextId::null()) { return; }
    context = recompui::create_context();
    context.open();
    context.set_captures_input(false);
    context.set_captures_mouse(false);
    auto* root = context.get_root_element();
    root->set_background_color(recompui::theme::color::Transparent);
    root->set_pointer_events(recompui::PointerEvents::None);
    panel = context.create_element<recompui::Element>(root);
    panel->set_position(recompui::Position::Absolute);
    panel->set_left(2, recompui::Unit::Percent);
    panel->set_bottom(2, recompui::Unit::Percent);
    panel->set_background_color(recompui::Color{9, 27, 25, 220});
    panel->set_border_radius(4);
    panel->set_pointer_events(recompui::PointerEvents::None);
    label = context.create_element<recompui::Label>(panel, "", recompui::LabelStyle::Small);
    label->set_position(recompui::Position::Absolute);
    for (auto*& segment : segments) {
        segment = context.create_element<recompui::Element>(panel);
        segment->set_position(recompui::Position::Absolute);
    }
    context.close();
}
}

void twine::vision_battery::publish(uint8_t* rdram, bool gameplay) {
    const auto status = read_status(rdram, gameplay);
    published.store((qol::lifecycle_epoch() << 9) |
        (uint64_t(status.mode) << 7) | status.percent, std::memory_order_release);
}

void twine::vision_battery::update_ui() {
    const uint64_t frame = published.load(std::memory_order_acquire);
    const auto mode = static_cast<Mode>((frame >> 7) & 3U);
    if ((frame >> 9) != qol::lifecycle_epoch() || mode == Mode::Off ||
            recompinput::game_input_disabled()) {
        if (visible) { recompui::hide_context(context); visible = false; }
        return;
    }
    int width = 0, height = 0;
    recompui::get_window_size(width, height);
    if (width <= 0 || height <= 0) { return; }
    initialize_ui();
    if (!visible || previous != frame || previous_height != height) {
        const unsigned percent = frame & 0x7FU;
        const auto color = mode == Mode::NightVision && percent <= 20
            ? recompui::Color{242, 187, 91, 255}
            : recompui::Color{158, 225, 191, 255};
        char text[48];
        if (mode == Mode::Xray) { std::snprintf(text, sizeof(text), "X-RAY  UNLIMITED"); }
        else { std::snprintf(text, sizeof(text), "NIGHT VISION  %u%%", percent); }
        const float scale = std::clamp(float(height) / 800.0f, 0.6f, 2.4f);
        context.open();
        panel->set_width(230 * scale);
        panel->set_height(54 * scale);
        label->set_left(10 * scale);
        label->set_top(6 * scale);
        label->set_font_size(17 * scale);
        label->set_color(color);
        label->set_text(text);
        for (unsigned i = 0; i < segments.size(); ++i) {
            auto* segment = segments[i];
            segment->set_left((10 + 21 * i) * scale);
            segment->set_bottom(10 * scale);
            segment->set_width(18 * scale);
            segment->set_height(10 * scale);
            segment->set_background_color(percent > i * 10
                ? color : recompui::Color{43, 66, 59, 255});
        }
        context.close();
        previous = frame;
        previous_height = height;
    }
    if (!visible) { recompui::show_context(context, {}); visible = true; }
}
