#pragma once

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include "save_state_file.hpp"

namespace twine::state {

class Writer {
public:
    Bytes bytes;
    void blob(std::span<const uint8_t> data) {
        if (data.size() > maximum_file_size) throw std::runtime_error("Oversized state owner");
        u32(uint32_t(data.size()));
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void u32(uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(uint8_t(value >> (i * 8))); }
    void u64(uint64_t value) { u32(uint32_t(value)); u32(uint32_t(value >> 32)); }
    template<class T> void scalar(T value) {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, float>);
        if constexpr (std::is_same_v<T, float>) u32(std::bit_cast<uint32_t>(value));
        else if constexpr (sizeof(T) <= 4) u32(uint32_t(value));
        else u64(uint64_t(value));
    }
    template<class... T> void fields(const T&... values) { (scalar(values), ...); }
};

class Reader {
    std::span<const uint8_t> remaining;
public:
    explicit Reader(std::span<const uint8_t> bytes) : remaining(bytes) {}
    std::span<const uint8_t> blob(size_t maximum) {
        const auto size = u32();
        if (size > maximum || size > remaining.size()) throw std::runtime_error("Invalid state owner extent");
        const auto result = remaining.first(size);
        remaining = remaining.subspan(size);
        return result;
    }
    uint32_t u32() {
        if (remaining.size() < 4) throw std::runtime_error("Truncated save-state section");
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(remaining[i]) << (i * 8);
        remaining = remaining.subspan(4);
        return value;
    }
    uint64_t u64() { const auto low = u32(); return uint64_t(low) | (uint64_t(u32()) << 32); }
    template<class T> T scalar() {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, float>);
        if constexpr (std::is_same_v<T, float>) return std::bit_cast<float>(u32());
        else if constexpr (std::is_enum_v<T>) return T(scalar<std::underlying_type_t<T>>());
        else if constexpr (sizeof(T) <= 4) {
            const auto bits = u32();
            if constexpr (std::is_signed_v<T>) {
                const auto value = std::bit_cast<int32_t>(bits);
                if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max())
                    throw std::runtime_error("Save-state integer is outside its field range");
                return T(value);
            } else {
                if (bits > uint32_t(std::numeric_limits<T>::max()))
                    throw std::runtime_error("Save-state integer or boolean is outside its field range");
                return T(bits);
            }
        } else {
            const auto bits = u64();
            if constexpr (std::is_signed_v<T>) return std::bit_cast<T>(bits);
            else return T(bits);
        }
    }
    template<class... T> void fields(T&... values) { ((values = scalar<T>()), ...); }
    void end() const { if (!remaining.empty()) throw std::runtime_error("Trailing save-state section data"); }
};

}
