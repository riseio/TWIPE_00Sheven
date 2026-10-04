#pragma once

#include "elements/ui_element.h"

namespace twine::radial::view {
void register_elements();

class Label : public recompui::Element {
    std::string current_text;
public:
    Label(recompui::ResourceId id, recompui::Element* parent);
    void set_text(std::string_view text) override;
};
}
