#ifndef TWINE_GAMEPLAY_INPUT_OWNER_HPP
#define TWINE_GAMEPLAY_INPUT_OWNER_HPP

#include <array>
#include <atomic>
#include <cstdint>

namespace twine::modern_input {

class GameplayInputOwner {
    std::array<std::atomic<uint64_t>, 4> published{};
    uint64_t tickEpoch = 0;
    uint8_t seen = 0;
public:
    void begin(uint64_t epoch) { tickEpoch = epoch; seen = 0; }
    void observe(unsigned player) {
        if (player >= published.size()) { return; }
        seen |= uint8_t(1U << player);
        published[player].store(tickEpoch, std::memory_order_release);
    }
    void finish() {
        for (unsigned i = 0; i < published.size(); ++i) {
            published[i].store((seen & (1U << i)) ? tickEpoch : 0,
                std::memory_order_release);
        }
    }
    bool active(uint64_t epoch) const {
        if (epoch == 0) { return false; }
        for (const auto& player : published) {
            if (player.load(std::memory_order_acquire) == epoch) { return true; }
        }
        return false;
    }
};
}
#endif
