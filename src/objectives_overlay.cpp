#include "objectives_overlay.hpp"

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "recompui/recompui.h"
#include "recompinput/input_state.h"
#include "twine_qol.hpp"

namespace {
twine::objectives::Toggle toggle;
std::atomic<uint64_t> request{0};
std::atomic_bool shown{false};
std::atomic<uint16_t> shown_count{0};
std::mutex snapshot_mutex;
twine::objectives::Frame published{};
twine::objectives::Frame previous{};
recompui::ContextId context = recompui::ContextId::null();
recompui::Label* contents = nullptr;
recompui::Element* panel = nullptr;
bool ui_visible = false;
int previous_width = 0, previous_height = 0;
float font_size = 22;

void initialize_ui() {
    if (context != recompui::ContextId::null()) { return; }
    context = recompui::create_context();
    context.open();
    context.set_captures_input(false);
    context.set_captures_mouse(false);
    auto* document = context.get_root_element();
    document->set_background_color(recompui::theme::color::Transparent);
    document->set_pointer_events(recompui::PointerEvents::None);
    panel = context.create_element<recompui::Element>(document);
    panel->set_position(recompui::Position::Absolute);
    panel->set_right(2, recompui::Unit::Percent);
    panel->set_top(2, recompui::Unit::Percent);
    panel->set_width(34, recompui::Unit::Percent);
    panel->set_max_width(520);
    panel->set_padding(20);
    panel->set_border_radius(8);
    panel->set_background_color(recompui::Color{9, 27, 25, 220});
    panel->set_pointer_events(recompui::PointerEvents::None);
    auto* title = context.create_element<recompui::Label>(panel,
        "MISSION OBJECTIVES", recompui::LabelStyle::Small);
    title->set_font_size(24);
    title->set_color(recompui::Color{158, 225, 191, 255});
    title->set_margin_bottom(14);
    contents = context.create_element<recompui::Label>(panel, "", recompui::LabelStyle::Small);
    contents->set_font_size(22);
    contents->set_white_space(recompui::WhiteSpace::Prewrap);
    contents->set_color(recompui::Color{240, 246, 241, 255});
    context.close();
}
}

void twine::objectives::update_input(bool available, bool down, uint64_t epoch) {
    const uint64_t next = (epoch << 1) | (toggle.update(available, down, epoch) ? 1U : 0U);
    const uint64_t old = request.exchange(next, std::memory_order_acq_rel);

}

void twine::objectives::publish(uint8_t* rdram, recomp_context* ctx, bool gameplay) {
    const uint64_t epoch = qol::lifecycle_epoch();
    if (!gameplay || request.load(std::memory_order_acquire) != ((epoch << 1) | 1U)) { return; }
    const auto frame = read_frame(rdram, ctx, epoch);

    std::unique_lock lock(snapshot_mutex, std::try_to_lock);
    if (lock.owns_lock()) { published = frame; }
}

bool twine::objectives::visible() {
    return request.load(std::memory_order_acquire) == ((qol::lifecycle_epoch() << 1) | 1U);
}
bool twine::objectives::displayed() { return shown.load(std::memory_order_acquire); }
uint16_t twine::objectives::displayed_count() { return shown_count.load(std::memory_order_acquire); }

void twine::objectives::update_ui() {
    if (!visible() || recompinput::game_input_disabled()) {
        if (ui_visible) { recompui::hide_context(context); ui_visible = false; }
        shown.store(false, std::memory_order_release);
        shown_count.store(0, std::memory_order_release);
        return;
    }
    Frame frame;
    { std::lock_guard lock(snapshot_mutex); frame = published; }
    if (frame.epoch != qol::lifecycle_epoch()) {
        if (ui_visible) { recompui::hide_context(context); ui_visible = false; }
        shown.store(false, std::memory_order_release);
        shown_count.store(0, std::memory_order_release);
        return;
    }
    initialize_ui();
    int width = 0, height = 0;
    recompui::get_window_size(width, height);
    if (width <= 0 || height <= 0) { return; }
    const bool changed = !ui_visible || frame != previous ||
        width != previous_width || height != previous_height;
    if (changed) {
        std::string text;
        if (!frame.valid) { text = "Objectives unavailable."; }
        else if (frame.count == 0) { text = "No current objectives."; }
        for (size_t i = 0; i < frame.count; ++i) {
            if (i != 0) { text += "\n\n"; }
            const auto& row = frame.rows[i];
            text += row.state == 2 ? "[Failed] " : "[Active] ";
            text += row.text.data();
        }
        context.open();
        font_size = 22;
        contents->set_font_size(font_size);
        contents->set_text(text);
        context.close();
        previous = frame;
        previous_width = width;
        previous_height = height;
    }
    else {

        context.open();
        const float panel_height = panel->get_client_height();
        const float available_height = height * 0.78f;
        if (panel_height > available_height && font_size > 12) {
            font_size = std::max(12.0f, font_size * available_height / panel_height - 0.5f);
            contents->set_font_size(font_size);
        }
        context.close();
    }
    if (!ui_visible) { recompui::show_context(context, {}); ui_visible = true; }
    shown.store(true, std::memory_order_release);
    shown_count.store(frame.valid ? frame.count : 0, std::memory_order_release);
}
