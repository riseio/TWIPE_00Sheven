#include "save_state_owner.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

#include "modern_grapple.hpp"
#include "rom_metadata.hpp"
#include "twine_qol.hpp"
#include "twine_recomp.h"

namespace {
constexpr uint32_t weapons = 0x800C469C;
constexpr uint32_t ammo = 0x800C7C14;
constexpr uint32_t stride = 232;
bool double_darts = false;
struct LaserCharge { uint32_t inventory = 0; unsigned quarters = 0; };
std::array<LaserCharge, 4> laser_charge{};
bool valid(uint32_t address, uint32_t size) {
    return (address & 3U) == 0 && twine::grapple::rdram_range_valid(address, size);
}
void set_float(uint8_t* rdram, unsigned weapon, unsigned offset, float value) {
    TWINE_MEM_W(offset, weapons + weapon * stride) = std::bit_cast<uint32_t>(value);
}
void magazine(uint8_t* rdram, unsigned weapon, unsigned rounds) {
    TWINE_MEM_B(0x82, weapons + weapon * stride) = rounds;
    TWINE_MEM_B(0x83, weapons + weapon * stride) = rounds;
}
}

extern "C" void twine_weapon_initialize(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx) { return; }

    const auto& stock = twine::rom::metadata();
    double_darts = twine::qol::settings().darts == twine::qol::DartAmmoMode::Double;
    for (unsigned id : {1U, 2U, 35U}) { set_float(rdram, id, 0xC, stock.weapons[id].damage * 2.0f); }
    set_float(rdram, 7, 0x20, 0.0f);
    magazine(rdram, 9, 60);
    magazine(rdram, 13, 45);
    magazine(rdram, 10, 40);
    magazine(rdram, 11, 50);
    magazine(rdram, 12, 50);
    magazine(rdram, 34, stock.weapons[34].magazine * (double_darts ? 2 : 1));

    for (unsigned id : {1U, 2U}) {
        TWINE_MEM_H(4 * id, ammo) = stock.ammo[id].pickup * 2;
        TWINE_MEM_H(4 * id + 2, ammo) = stock.ammo[id].maximum * 2;
    }
    TWINE_MEM_H(4 * 16 + 2, ammo) = stock.ammo[16].maximum * (double_darts ? 2 : 1);

    for (unsigned id : {3U, 8U}) {
        TWINE_MEM_B(0x36, weapons + id * stride) = (unsigned(stock.weapons[id].reload) * 2 + 2) / 3;
    }
    const uint32_t actor = uint32_t(ctx->r4);
    if (!valid(actor, 0x70)) { return; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, actor);
    if (!valid(inventory, 0x184)) { return; }
    const unsigned player = TWINE_MEM_BU(0x182, inventory);
    if (player < laser_charge.size()) { laser_charge[player] = {inventory, 0}; }
}

extern "C" uint32_t twine_weapon_pickup_rounds(uint32_t item, uint32_t native_rounds) {
    const unsigned factor = item == 9 || item == 13 || (item == 34 && double_darts) ? 2 : 1;
    return std::min(native_rounds, 16383U) * factor;
}

extern "C" uint32_t twine_ammo_pickup_rounds(uint32_t type, uint32_t native_rounds) {
    const unsigned factor = type == 1 || type == 2 || (type == 16 && double_darts) ? 2 : 1;
    return std::min(native_rounds, 16383U) * factor;
}

extern "C" uint32_t twine_weapon_pickup_magazine(uint32_t item, uint32_t capacity) {

    switch (item) {
    case 9: case 10: case 11: case 12: case 13: case 34:
        return twine::rom::metadata().weapons[item].magazine;
    default: return capacity;
    }
}

extern "C" void twine_laser_ammo_cost(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t inventory = uint32_t(ctx->r16);
    if (!valid(inventory, 0x318) || uint32_t(ctx->r5) != inventory + 35 * 8 ||
            ctx->r18 <= 0 || ctx->r18 > 127) { return; }
    const unsigned player = TWINE_MEM_BU(0x182, inventory);
    if (player >= laser_charge.size()) { return; }
    auto& charge = laser_charge[player];
    if (charge.inventory != inventory) { charge = {inventory, 0}; }

    const unsigned quarters = charge.quarters + unsigned(ctx->r18);
    ctx->r18 = quarters / 4;
    charge.quarters = quarters % 4;
}

extern "C" void twine_reload_animation_step(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t model = uint32_t(ctx->r20);
    if (!valid(model, 0x80) || TWINE_MEM_HU(0x78, model) != 0x30 ||
            TWINE_MEM_HU(0x7A, model) != 5) { return; }
    const uint32_t actor = TWINE_MEM_W(0x18, model);
    if (!valid(actor, 0x70)) { return; }
    const uint32_t inventory = TWINE_MEM_W(0x6C, actor);
    const uint32_t item = TWINE_MEM_W(0x68, actor);
    if (!valid(inventory, 0xAC) || !valid(item, 0x10) ||
            uint32_t(TWINE_MEM_W(0xA8, inventory)) != model) { return; }
    const unsigned id = TWINE_MEM_BU(0xE, item);
    if ((id == 3 || id == 8) && std::isfinite(ctx->f0.fl)) {
        ctx->f0.fl *= 1.5f;
    }
}

twine::state::Bytes twine::state::capture_weapons(uint8_t*) {
    Writer out; out.fields(uint32_t(1), double_darts);
    for (const auto& value : laser_charge) out.fields(value.inventory, value.quarters);
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_weapons(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid weapon state schema");
    const bool darts = in.scalar<bool>();
    std::array<LaserCharge, 4> charges;
    for (auto& value : charges) {
        in.fields(value.inventory, value.quarters);
        if (value.quarters > 3 || (value.inventory && !guest_range(value.inventory, 0x188)))
            throw std::runtime_error("Invalid laser charge state");
    }
    in.end();
    return prepared_owner([darts, charges]() noexcept { double_darts = darts; laser_charge = charges; });
}
