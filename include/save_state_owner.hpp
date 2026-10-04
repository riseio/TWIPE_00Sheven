#pragma once
#include "save_state_codec.hpp"
#include <memory>
#include <utility>

namespace twine::state {
class PreparedOwner {
public:
    virtual ~PreparedOwner() = default;
    virtual void commit() noexcept = 0;
};
template<class F> class OwnerCommit final : public PreparedOwner {
    F apply;
public:
    explicit OwnerCommit(F&& function) : apply(std::move(function)) {}
    void commit() noexcept override { apply(); }
};
template<class F> std::unique_ptr<PreparedOwner> prepared_owner(F&& function) {
    static_assert(noexcept(function()), "State commits must not fail");
    return std::make_unique<OwnerCommit<F>>(std::forward<F>(function));
}

Bytes capture_grapple(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_grapple(std::span<const uint8_t>);
Bytes capture_health(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_health(std::span<const uint8_t>);
Bytes capture_weapons(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_weapons(std::span<const uint8_t>);
Bytes capture_input(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_input(std::span<const uint8_t>);
Bytes capture_storage(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_storage(std::span<const uint8_t>);
Bytes capture_prompts(uint8_t*);
std::unique_ptr<PreparedOwner> prepare_prompts(std::span<const uint8_t>);
std::pair<uint32_t, uint32_t> prompt_heap_owner();
Bytes capture_profile_session();
std::unique_ptr<PreparedOwner> prepare_profile_session(std::span<const uint8_t>);
Bytes capture_radial();
std::unique_ptr<PreparedOwner> prepare_radial(std::span<const uint8_t>);
inline bool guest_range(uint32_t p, uint32_t size) {
    return p >= 0x80000000U && uint64_t(p) + size <= 0x80800000ULL;
}
}
