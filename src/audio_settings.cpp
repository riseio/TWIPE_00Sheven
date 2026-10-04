#include "audio_settings.hpp"
#include "audio_host.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "elements/ui_button.h"
#include "elements/ui_label.h"
#include "elements/ui_select.h"

#include <algorithm>

namespace twine::audio_settings {
namespace {
constexpr const char* output_key = "output_device";
constexpr const char* default_value = "@default";

class OutputControls : public recompui::Element {
    recompui::Element* slot = nullptr;
    recompui::Select* selector = nullptr;
    recompui::Label* status = nullptr;
    std::vector<std::string> names;
    std::string selected;
    uint64_t revision = 0;

    void update() {
        queue_update();
        if (revision == audio_host::output_revision()) return;
        const auto state = audio_host::output_devices();
        revision = state.revision;
        auto choices = state.names;
        if (!state.selected.empty() && std::find(choices.begin(), choices.end(), state.selected) == choices.end())
            choices.push_back(state.selected);
        if (!selector || choices != names) {
            names = std::move(choices);
            slot->clear_children();
            std::vector<recompui::SelectOption> options{{"System default", default_value}};
            for (const auto& name : names) {
                const bool present = std::find(state.names.begin(), state.names.end(), name) != state.names.end();
                options.emplace_back(name + (present ? "" : " (unavailable)"), "device:" + name);
            }
            selector = recompui::get_current_context().create_element<recompui::Select>(slot, options,
                state.selected.empty() ? default_value : "device:" + state.selected);
            selector->add_change_callback([](recompui::SelectOption& option, int) {
                auto& config = recompui::config::get_sound_config();
                const std::string name = option.value == default_value ? "" : option.value.substr(7);
                if (std::get<std::string>(config.get_option_value(output_key)) == name) return;
                const auto previous = config.get_option_value(output_key);
                config.update_option_value(output_key, name);
                if (!config.save_config()) {
                    config.update_option_value(output_key, previous);
                    recompui::open_info_prompt("Could not save audio output",
                        "Check that the game data folder is writable, then try again.", "OK", [] {});
                }
            });
        } else if (selected != state.selected) {
            selector->set_selection(state.selected.empty() ? default_value : "device:" + state.selected);
        }
        selected = state.selected;
        std::string text = state.active.empty() ? "No audio output is open." : "Playing through: " + state.active;
        if (state.fallback) text += "\nUsing the system default until your selected device returns.";
        if (!state.error.empty()) text += "\n" + state.error;
        status->set_text(text);
    }

    void process_event(const recompui::Event& event) override {
        if (event.type == recompui::EventType::Update) update();
    }
public:
    OutputControls(recompui::ResourceId id, recompui::Element* parent)
        : Element(id, parent, recompui::Events(recompui::EventType::Update)) {
        set_display(recompui::Display::Flex);
        set_flex_direction(recompui::FlexDirection::Column);
        set_width(100, recompui::Unit::Percent);
        set_padding(12);
        set_gap(12);
        set_as_navigation_container(recompui::NavigationType::Vertical);
        auto context = recompui::get_current_context();
        context.create_element<recompui::Label>(this, "Audio Output", recompui::theme::Typography::LabelMD);
        slot = context.create_element<recompui::Element>(this);
        slot->set_width(100, recompui::Unit::Percent);
        status = context.create_element<recompui::Label>(this, "", recompui::theme::Typography::LabelXS);
        status->set_width(100, recompui::Unit::Percent);
        auto* test = context.create_element<recompui::Button>(this, "Test Output", recompui::ButtonStyle::Secondary);
        test->add_pressed_callback(audio_host::test_output);
        auto* refresh = context.create_element<recompui::Button>(this, "Refresh / Retry Output", recompui::ButtonStyle::Secondary);
        refresh->add_pressed_callback(audio_host::refresh_outputs);
        update();
    }
};
}

void create() {
    auto& config = recompui::config::create_sound_tab(recompui::config::sound::tab_name,
        [](recompui::ContextId context, recompui::Element* parent) { context.create_element<OutputControls>(parent); });

    config.add_string_option(output_key, "Audio Output", "", "", true);
    config.add_option_change_callback(output_key, [](auto value, auto, auto context) {
        if (context != recomp::config::OptionChangeContext::Temporary) {
            if (!audio_host::select_output(std::get<std::string>(value))) {
                audio_host::select_output("");

            }
        }
    });
}
}
