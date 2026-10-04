#include "save_state_file.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace twine::state {
namespace {
constexpr std::array<uint8_t, 8> magic{'T', 'W', 'I', 'N', 'E', 'S', 'T', 'A'};
constexpr uint32_t format = 2;
constexpr size_t header_size = 96;
constexpr size_t checksum_offset = 92;
constexpr Fingerprint rom{0x72,0xe3,0xe7,0xb4,0xff,0x16,0x15,0xbc,0x17,0x33,0x6d,0x3d,0x10,0xe1,0x8a,0xa9,0x34,0x28,0x98,0xdb,0x06,0x3a,0xce,0x68,0x92,0x2a,0x47,0xf5,0xe4,0x6c,0x48,0xb1};

constexpr auto crc_table = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
        }
        table[i] = value;
    }
    return table;
}();

uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t value = ~0U;
    for (size_t i = 0; i < bytes.size(); ++i) {

        const uint8_t byte = (i >= checksum_offset && i < header_size) ? 0 : bytes[i];
        value = crc_table[(value ^ byte) & 0xffU] ^ (value >> 8);
    }
    return ~value;
}

void append(Bytes& bytes, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

uint64_t take(std::span<const uint8_t> bytes, size_t& offset, size_t count) {
    if (count > bytes.size() || offset > bytes.size() - count) {
        throw std::runtime_error("Truncated save state");
    }
    uint64_t result = 0;
    for (size_t i = 0; i < count; ++i) {
        result |= uint64_t(bytes[offset++]) << (i * 8);
    }
    return result;
}

uint8_t hex_digit(char c) {
    if (c >= '0' && c <= '9') return uint8_t(c - '0');
    if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return uint8_t(c - 'A' + 10);
    throw std::runtime_error("Invalid save-state source fingerprint");
}
}

Fingerprint parse_fingerprint(const std::string& hex) {
    if (hex.size() != 64) throw std::runtime_error("Invalid save-state source fingerprint length");
    Fingerprint result{};
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = uint8_t((hex_digit(hex[i * 2]) << 4) | hex_digit(hex[i * 2 + 1]));
    }
    return result;
}

Bytes encode(const Image& image) {
    uint64_t total = header_size;
    for (const auto& section : image.sections) {
        if (section.empty() || total > maximum_file_size - 8 ||
            section.size() > maximum_file_size - total - 8) {
            throw std::runtime_error("Missing or oversized save-state section");
        }
        total += 8 + section.size();
    }
    Bytes bytes;
    bytes.reserve(static_cast<size_t>(total));
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    append(bytes, format, 4);
    append(bytes, section_count, 4);
    bytes.insert(bytes.end(), image.source.begin(), image.source.end());
    bytes.insert(bytes.end(), rom.begin(), rom.end());
    append(bytes, state_abi, 4);
    append(bytes, total, 8);
    append(bytes, 0, 4);
    for (size_t i = 0; i < section_count; ++i) {
        append(bytes, i + 1, 4);
        append(bytes, image.sections[i].size(), 4);
        bytes.insert(bytes.end(), image.sections[i].begin(), image.sections[i].end());
    }
    const uint32_t crc = checksum(bytes);
    for (size_t i = 0; i < 4; ++i) bytes[checksum_offset + i] = uint8_t(crc >> (i * 8));
    return bytes;
}

Image decode(std::span<const uint8_t> bytes) {
    if (bytes.size() < header_size || bytes.size() > maximum_file_size ||
        !std::equal(magic.begin(), magic.end(), bytes.begin())) {
        throw std::runtime_error("Invalid save-state signature or file size");
    }
    size_t offset = magic.size();
    if (take(bytes, offset, 4) != format || take(bytes, offset, 4) != section_count) {
        throw std::runtime_error("Unsupported save-state format");
    }
    if (!std::equal(rom.begin(), rom.end(), bytes.begin() + 48)) {
        throw std::runtime_error("Save state belongs to an unsupported ROM");
    }
    offset = 80;
    if (take(bytes, offset, 4) != state_abi) throw std::runtime_error("Save state uses an incompatible game state ABI");
    if (take(bytes, offset, 8) != bytes.size()) throw std::runtime_error("Truncated or extended save state");
    if (take(bytes, offset, 4) != checksum(bytes)) throw std::runtime_error("Save-state checksum mismatch");

    std::array<std::span<const uint8_t>, section_count> sections;
    for (size_t i = 0; i < section_count; ++i) {
        if (take(bytes, offset, 4) != i + 1) throw std::runtime_error("Missing, duplicate or unordered save-state section");
        const uint64_t size = take(bytes, offset, 4);
        if (size == 0 || size > bytes.size() - offset) throw std::runtime_error("Invalid save-state section length");
        sections[i] = bytes.subspan(offset, static_cast<size_t>(size));
        offset += static_cast<size_t>(size);
    }
    if (offset != bytes.size()) throw std::runtime_error("Unexpected trailing save-state data");
    Image result;
    std::copy_n(bytes.begin() + 16, result.source.size(), result.source.begin());
    for (size_t i = 0; i < section_count; ++i) result.sections[i].assign(sections[i].begin(), sections[i].end());
    return result;
}

Image read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open save state");
    const auto length = stream.tellg();
    if (length < std::streamoff(header_size) || length > std::streamoff(maximum_file_size)) {
        throw std::runtime_error("Invalid save-state file size");
    }
    Bytes bytes(static_cast<size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
        stream.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Save-state read failed or file changed during read");
    }
    return decode(bytes);
}

WriteResult write_file(const std::filesystem::path& path, const Image& image) {
    const auto bytes = encode(image);
    return files::write_atomic(path, bytes);
}
}
