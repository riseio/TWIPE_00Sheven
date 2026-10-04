#pragma once
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>
#include <cstdint>

namespace twine::textures {
inline constexpr size_t prepared_texture_count = 8417;
struct PreparationCancelled final : std::exception {
    const char *what() const noexcept override { return "Texture preparation cancelled"; }
};
using PreparationProgress = std::function<void(size_t)>;
using VerificationProgress = std::function<void(uint64_t, uint64_t)>;

bool has_pack_cache(const std::filesystem::path& folder);
bool has_prepared_pack(const std::filesystem::path& folder);
std::filesystem::path find_prepared_pack(const std::filesystem::path& folder, std::stop_token stop = {},
                                        const VerificationProgress& progress = {});
void write_pack(std::span<const uint8_t> rom, FILE *output, const PreparationProgress &progress = {},
                std::stop_token stop = {});
std::filesystem::path prepare_pack(std::span<const uint8_t> rom, const std::filesystem::path &folder,
                                   const PreparationProgress &progress = {}, std::stop_token stop = {},
                                   const VerificationProgress& verification = {});

void remove_pack_cache(const std::filesystem::path &folder);
}
