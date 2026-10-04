#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace twine::textures {
inline constexpr uint64_t pack_size = 3167785221ULL;
inline constexpr uint64_t pack_hash = 0xCBAF412FFC2CB834ULL;
std::filesystem::path pack_path(const std::filesystem::path& folder);
std::filesystem::path pack_receipt_path(const std::filesystem::path& folder);
void validate_pack_location(const std::filesystem::path& folder);
std::string pack_file_identity(const std::filesystem::path& path);

std::filesystem::path installed_pack(const std::filesystem::path& folder);

void record_pack_installation(const std::filesystem::path& folder, std::string_view identity);
}
