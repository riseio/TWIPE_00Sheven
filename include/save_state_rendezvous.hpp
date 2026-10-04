#pragma once

#include <array>
#include <cstdint>
#include "recomp.h"
#include "save_state_game.hpp"

namespace twine::state {

enum class Action : uint8_t { None, Save, Restore };
enum class Root : uint32_t { Game, Video, Audio, Dma, Scheduler };

struct RootState {
    uint32_t thread = 0;
    std::array<uint64_t, 32> gpr{};
    std::array<uint64_t, 32> fpr{};
    uint64_t hi = 0, lo = 0;
    uint32_t status = 0;
    uint32_t float_mode = 0;
    std::array<uint64_t, 4> locals{};
    uint32_t cop1 = 0;
};
using Roots = std::array<RootState, 5>;

RootState capture_root(uint32_t thread, const recomp_context& context,
                      const uint64_t* locals, uint32_t cop1);
void validate_root(const RootState& saved, const RootState& current);
void restore_root(const RootState& saved, recomp_context& context, uint64_t* locals);

using Transaction = void(Action, uint8_t*, const Roots&);
using Completion = void(Action, const char* error) noexcept;

void stage_restore_roots(const Roots& saved);
const GameState& captured_game_state();
void stage_restore_game(uint8_t* rdram, const GameState& saved);
void initialize_rendezvous(Transaction* transaction, Completion* completion);
bool request(Action action);
void idle_checkpoint(uint8_t* rdram);

}

extern "C" uint32_t twine_state_requested();
extern "C" void twine_state_root(uint8_t* rdram, recomp_context* ctx,
                                uint32_t root, uint64_t* locals);
