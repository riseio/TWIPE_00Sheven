#pragma once

#include "steam_deck_grips.hpp"
#include <array>
#include <bit>

namespace twine::deck_motion {
using FeatureReport = std::array<uint8_t, 65>;
inline constexpr uint16_t raw_sensors = 0x18;

inline FeatureReport mode_request(bool write, uint16_t value = 0) {
    FeatureReport report{};
    report[1] = write ? 0x87 : 0x89;
    report[2] = 3;
    report[3] = 0x30;
    report[4] = uint8_t(value);
    report[5] = uint8_t(value >> 8);
    return report;
}

inline bool read_mode(const FeatureReport& report, size_t size, uint16_t& value) {
    if (size < 6 || report[0] != 0 || report[1] != 0x89 ||
            report[2] != 3 || report[3] != 0x30) { return false; }
    value = uint16_t(report[4]) | uint16_t(report[5]) << 8;
    return true;
}

class ModeLease {
    uint16_t original_ = 0, applied_ = 0;
    bool changed_ = false;
public:
    template<class Read, class Write>
    bool enable(Read read, Write write) {
        uint16_t current = 0;
        if (!read(current)) { return false; }
        if (changed_ && current != applied_) { changed_ = false; }
        if ((current & raw_sensors) == raw_sensors) { return true; }
        const uint16_t wanted = current | raw_sensors;
        if (!write(wanted)) { return false; }
        original_ = current;
        applied_ = wanted;
        changed_ = true;
        return true;
    }

    template<class Read, class Write>
    bool release(Read read, Write write) {
        if (!changed_) { return true; }
        uint16_t current = 0;
        if (!read(current)) { return false; }
        if (current == applied_ && !write(original_)) { return false; }
        changed_ = false;
        return true;
    }
};

void initialize();
void shutdown();
void set_source(int hidraw_index, bool enabled, bool motion_seen);
void reset_source();

struct Sample {
    uint64_t timestamp_us = 0;
    std::array<float, 3> acceleration{}, angular_velocity{};
};

inline bool decode(const uint8_t* report, size_t size, Sample& sample) {
    if (!deck_grips::is_deck_report(report, size)) { return false; }

    bool populated = false;
    for (size_t byte = 24; byte < 36; ++byte) { populated |= report[byte] != 0; }
    if (!populated) { return false; }
    uint32_t packet = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { packet |= uint32_t(report[4 + byte]) << (byte * 8); }
    sample.timestamp_us = uint64_t(packet) * 4000;
    const auto read = [&](size_t offset) {
        return float(std::bit_cast<int16_t>(uint16_t(report[offset] | uint16_t(report[offset + 1]) << 8)));
    };
    constexpr float acceleration_scale = 2.0f * 9.80665f / 32768.0f;
    constexpr float gyro_scale = 2000.0f * 0.017453292519943295f / 32768.0f;
    sample.acceleration = {read(24) * acceleration_scale, read(28) * acceleration_scale, -read(26) * acceleration_scale};
    sample.angular_velocity = {read(30) * gyro_scale, read(34) * gyro_scale, -read(32) * gyro_scale};
    return true;
}
}
