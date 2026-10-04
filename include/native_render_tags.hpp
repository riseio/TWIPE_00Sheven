#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include "shared/rt64_native_render_ids.h"

namespace twine::render {

struct TransformOwner {
    uint32_t instance;
    uint32_t resource;
    uint32_t part;
    uint32_t domain;
    bool operator==(const TransformOwner&) const = default;
};

class TransformIds {
public:
    void begin_frame() {
        if (++frame_ == 0) { throw std::runtime_error("Render identity frame overflow"); }
    }

    void cut() {
        for (auto& entry : entries_) { entry = {}; }
    }

    void retire(uint32_t address, uint32_t size) {
        const auto contains = [=](uint32_t pointer) {
            return pointer >= address && uint64_t(pointer) - address < size;
        };
        for (auto& entry : entries_) {
            if (entry.id && (contains(entry.owner.instance) || contains(entry.owner.resource))) {

                entry.id = next(entry.owner);
            }
        }
    }

    uint32_t get(TransformOwner owner) {

        uint32_t hash = 2166136261U;
        for (uint32_t word : {owner.instance, owner.resource, owner.part, owner.domain}) {
            hash = (hash ^ word) * 16777619U;
        }
        Entry* vacant = nullptr;
        for (std::size_t probe = 0; probe < entries_.size(); ++probe) {
            auto& entry = entries_[(hash + probe) % entries_.size()];
            if (entry.id == 0) {
                if (!vacant) { vacant = &entry; }
                break;
            }
            if (entry.owner == owner) {
                if (entry.frame + 1 < frame_) { entry.id = next(owner); }
                entry.frame = frame_;
                return entry.id;
            }
            if (entry.frame + 1 < frame_ && !vacant) { vacant = &entry; }
        }
        if (!vacant) { throw std::runtime_error("Native render identity capacity exceeded"); }
        *vacant = {owner, frame_, next(owner)};
        return vacant->id;
    }

private:
    struct Entry {
        TransformOwner owner{};
        uint64_t frame = 0;
        uint32_t id = 0;
    };
    uint32_t next(const TransformOwner& owner) {
        if (next_id_ > RT64::NativeTransformId::MaxSequence) {
            throw std::runtime_error("Render identity exhausted");
        }

        if (owner.domain < 1 || owner.domain > 10) {
            throw std::runtime_error("Invalid native render identity domain");
        }
        return next_id_++ | (owner.domain >= 3 ? RT64::NativeTransformId::Dynamic : 0U);
    }
    std::array<Entry, 16384> entries_{};
    uint64_t frame_ = 0;
    uint32_t next_id_ = 1;
};

}
