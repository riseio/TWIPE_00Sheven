#pragma once

#include "save_state_file.hpp"
#include <memory>

namespace twine::state {
struct GameState {
    Bytes enhancements, modernization;
};

Bytes capture_enhancements(uint8_t* rdram);
Bytes capture_modernization(uint8_t* rdram);
class PreparedEnhancements {
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend Bytes capture_enhancements(uint8_t*);
public:
    explicit PreparedEnhancements(std::span<const uint8_t> saved);
    ~PreparedEnhancements();
    void commit() noexcept;
};
class PreparedModernization {
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend Bytes capture_modernization(uint8_t*);
public:
    PreparedModernization(uint8_t* rdram, std::span<const uint8_t> saved);
    ~PreparedModernization();
    void commit() noexcept;
};
}
