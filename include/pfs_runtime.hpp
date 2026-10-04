#pragma once

#include <filesystem>

namespace twine::pfs_runtime {

void initialize(const std::filesystem::path& config_path);

}
