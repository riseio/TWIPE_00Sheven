#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace twine::render {

class CameraHistory {
public:
    bool update(uint64_t frame, uint32_t camera, uint32_t controller,
            const std::array<float, 3>& position) {
        for (float value : position) {
            if (!std::isfinite(value)) { throw std::runtime_error("Invalid native camera position"); }
        }
        Entry* selected = nullptr;
        for (auto& entry : entries_) {
            if (entry.camera == camera) { selected = &entry; break; }
            if (!selected && (entry.camera == 0 || entry.frame + 1 < frame)) { selected = &entry; }
        }
        if (!selected) { throw std::runtime_error("Native camera history capacity exceeded"); }
        auto& entry = *selected;
        if (entry.camera == camera && entry.controller == controller && entry.frame == frame) { return entry.cut; }
        bool cut = entry.camera != camera || entry.controller != controller || entry.frame + 1 != frame;
        std::array<float, 3> velocity{};
        if (!cut) {
            float prediction_error = 0;
            for (size_t i = 0; i < position.size(); ++i) {
                velocity[i] = position[i] - entry.position[i];
                const float error = velocity[i] - entry.velocity[i];
                prediction_error += error * error;
            }

            constexpr float script_cut_distance = 100.0f / 64.0f;
            cut = controller == 0 && prediction_error > script_cut_distance * script_cut_distance;
        }
        entry.camera = camera; entry.controller = controller; entry.frame = frame;
        entry.position = position;
        entry.velocity = cut ? std::array<float, 3>{} : velocity;
        entry.cut = cut;
        if (cut) { entry.rotation_valid = false; }
        return cut;
    }

    bool update_rotation(uint64_t frame, uint32_t camera, const std::array<float, 9>& rotation) {
        for (float value : rotation) {
            if (!std::isfinite(value)) { throw std::runtime_error("Invalid native camera rotation"); }
        }
        for (auto& entry : entries_) {
            if (entry.camera != camera || entry.frame != frame) { continue; }
            if (entry.rotation_valid && entry.rotation_frame == frame) { return entry.cut; }
            if (!entry.cut && entry.controller == 0 && entry.rotation_valid && entry.rotation_frame + 1 == frame) {

                double trace = 0;
                for (size_t i = 0; i < rotation.size(); ++i) {
                    trace += double(entry.rotation[i]) * rotation[i];
                }
                entry.cut = trace < 1;
            }
            entry.rotation = rotation;
            entry.rotation_frame = frame;
            entry.rotation_valid = true;
            return entry.cut;
        }
        throw std::runtime_error("Camera rotation has no current position owner");
    }

    void reset() { entries_ = {}; }
    void retire(uint32_t address, uint32_t size) {
        const auto contains = [=](uint32_t pointer) {
            return pointer >= address && uint64_t(pointer) - address < size;
        };
        for (auto& entry : entries_) {

            if (contains(entry.camera) || contains(entry.controller)) { entry = {}; }
        }
    }

private:
    struct Entry {
        uint32_t camera = 0, controller = 0;
        uint64_t frame = 0;
        std::array<float, 3> position{}, velocity{};
        bool cut = true;
        uint64_t rotation_frame = 0;
        std::array<float, 9> rotation{};
        bool rotation_valid = false;
    };
    std::array<Entry, 24> entries_{};
};

}
