#pragma once

#include "save_state_file.hpp"
#include "save_state_rendezvous.hpp"
#include "ultramodern/state.hpp"

namespace twine::state {

inline constexpr uint32_t state_rdram_size = 8U * 1024 * 1024;

struct MemoryState {
    Bytes memory;
    Bytes threads;
};
struct PreparedMemory {
    Bytes memory;
    Roots roots;
};

MemoryState capture_memory(std::span<const uint8_t> memory,
    std::span<const ultramodern::state::ThreadBinding> bindings, const Roots& roots);
PreparedMemory prepare_memory(const MemoryState& saved,
    std::span<const uint8_t> current_memory,
    std::span<const ultramodern::state::ThreadBinding> current_bindings,
    const Roots& current_roots);

}
