#include "save_state_game.hpp"
#include "save_state_owner.hpp"
#include <array>

namespace twine::state {
namespace {
struct Owner {
    Bytes (*capture)(uint8_t*);
    std::unique_ptr<PreparedOwner> (*prepare)(std::span<const uint8_t>);
};
constexpr std::array owners{
    Owner{capture_input, prepare_input}, Owner{capture_grapple, prepare_grapple},
    Owner{capture_health, prepare_health}, Owner{capture_weapons, prepare_weapons},
    Owner{capture_storage, prepare_storage}, Owner{capture_prompts, prepare_prompts}};
}
struct PreparedEnhancements::Impl {
    std::array<std::unique_ptr<PreparedOwner>, owners.size()> values;
};
Bytes capture_enhancements(uint8_t* rdram) {
    Writer out;
    out.fields(uint32_t(1), uint32_t(owners.size()));
    for (const auto& owner : owners) out.blob(owner.capture(rdram));
    return std::move(out.bytes);
}
PreparedEnhancements::PreparedEnhancements(std::span<const uint8_t> bytes)
    : impl(std::make_unique<Impl>()) {
    Reader in(bytes);
    if (in.u32() != 1 || in.u32() != owners.size()) throw std::runtime_error("Incompatible gameplay state schema");
    for (size_t i = 0; i < owners.size(); ++i) impl->values[i] = owners[i].prepare(in.blob(1024 * 1024));
    in.end();
}
PreparedEnhancements::~PreparedEnhancements() = default;
void PreparedEnhancements::commit() noexcept { for (auto& owner : impl->values) owner->commit(); }
}
