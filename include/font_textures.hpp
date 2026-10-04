#pragma once
#include <filesystem>
#include <memory>
#include "font_reconstruction.hpp"

namespace twine::fonts {
void initialize(const std::filesystem::path& config_path);
void reset();
std::shared_ptr<const Atlas> menu_atlas();
size_t prepare_cache(const std::filesystem::path& path, const Atlases& atlases);
}
