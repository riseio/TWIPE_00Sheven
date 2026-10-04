#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <cstdio>
#include <functional>
namespace twine::files {

struct WriteResult { bool durable = true; std::string warning; };

WriteResult write_atomic(const std::filesystem::path&, std::span<const uint8_t>);

WriteResult write_atomic_stream(const std::filesystem::path&, uint64_t maximum_size,
    const std::function<void(FILE*)>& produce);
}
