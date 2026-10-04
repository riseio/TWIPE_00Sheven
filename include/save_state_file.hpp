#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include "atomic_file.hpp"

namespace twine::state {

enum class Section : uint32_t {
    Memory = 1, Threads, Overlays, Timers, Events,
    Audio, Renderer, Enhancements, Modernization, SaveMedia,
};
inline constexpr size_t section_count = 10;
inline constexpr uint64_t maximum_file_size = 128ULL * 1024 * 1024;

inline constexpr uint32_t state_abi = 1;
using Fingerprint = std::array<uint8_t, 32>;
using Bytes = std::vector<uint8_t>;

struct Image {
    Fingerprint source{};
    std::array<Bytes, section_count> sections;
    Bytes& operator[](Section section) { return sections.at(size_t(section) - 1); }
    const Bytes& operator[](Section section) const { return sections.at(size_t(section) - 1); }
};

Fingerprint parse_fingerprint(const std::string& hex);
Bytes encode(const Image& image);

Image decode(std::span<const uint8_t> bytes);
Image read_file(const std::filesystem::path& path);

using WriteResult = files::WriteResult;

WriteResult write_file(const std::filesystem::path& path, const Image& image);

}
