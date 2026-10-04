#pragma once
#include <filesystem>
#include <string>
namespace recompui { class ContextId; class Element; }

namespace twine::textures {
void initialize(const std::filesystem::path& config_path, std::u8string game_id);
void generate();
bool available();
void configure();
void select(bool enhanced);
void load_selected();
void shutdown();
void create_controls(recompui::ContextId context, recompui::Element* parent);
}
