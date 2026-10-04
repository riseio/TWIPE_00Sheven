#pragma once

#include "save_state_file.hpp"
#include <memory>
#include <span>

namespace twine::state {

Bytes capture_overlays();
class PreparedOverlays {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    PreparedOverlays(std::span<const uint8_t> saved);
    ~PreparedOverlays();
    PreparedOverlays(PreparedOverlays&&) noexcept;
    PreparedOverlays& operator=(PreparedOverlays&&) noexcept;
    void commit() noexcept;
};
}
