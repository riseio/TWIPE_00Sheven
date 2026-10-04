#pragma once

#include <cstdint>

namespace recomp::config {
class Config;
}

namespace twine::cheats {

inline constexpr char config_id[] = "cheats";
inline constexpr char invulnerability_key[] = "invulnerability";
inline constexpr char all_weapons_key[] = "all_weapons";
inline constexpr char all_gadgets_key[] = "all_gadgets";
inline constexpr char all_missions_key[] = "all_missions";
inline constexpr char infinite_oxygen_key[] = "infinite_oxygen";
inline constexpr char no_fall_damage_key[] = "no_fall_damage";
inline constexpr char fall_damage_migration_key[] = "fall_damage_migrated_v1";

inline constexpr uint32_t main_menu_definition = 0x800BD8C4U;
inline constexpr uint32_t main_menu_entry = 0x800B5B7CU;
inline constexpr uint16_t stock_load_save_type = 81U;
inline constexpr uint16_t cheats_type = 70U;
inline constexpr uint32_t stock_load_save_callback = 0x80025C54U;
inline constexpr uint32_t generic_menu_callback = 0x800294E4U;

enum class Cheat : uint8_t {
    Invulnerability = 1U << 0,
    AllWeapons = 1U << 1,
    AllGadgets = 1U << 2,
    AllMissions = 1U << 3,
    InfiniteOxygen = 1U << 4,
    NoFallDamage = 1U << 5,
};

inline constexpr uint8_t all_mask =
    static_cast<uint8_t>(Cheat::Invulnerability) |
    static_cast<uint8_t>(Cheat::AllWeapons) |
    static_cast<uint8_t>(Cheat::AllGadgets) |
    static_cast<uint8_t>(Cheat::AllMissions) |
    static_cast<uint8_t>(Cheat::InfiniteOxygen) |
    static_cast<uint8_t>(Cheat::NoFallDamage);

constexpr bool enabled(uint8_t mask, Cheat cheat) {
    return (mask & static_cast<uint8_t>(cheat)) != 0;
}

struct NativeState {
    uint32_t invulnerability;
    uint8_t all_weapons;
    uint8_t all_gadgets;
    uint8_t all_missions;
};

constexpr uint8_t with_enabled(uint8_t mask, Cheat cheat, bool enabled) {
    const uint8_t bit = static_cast<uint8_t>(cheat);
    return enabled
        ? static_cast<uint8_t>(mask | bit)
        : static_cast<uint8_t>(mask & static_cast<uint8_t>(~bit));
}

constexpr NativeState native_state(uint8_t mask) {
    return {
        (mask & static_cast<uint8_t>(Cheat::Invulnerability)) != 0 ? 1U : 0U,
        static_cast<uint8_t>(
            (mask & static_cast<uint8_t>(Cheat::AllWeapons)) != 0),
        static_cast<uint8_t>(
            (mask & static_cast<uint8_t>(Cheat::AllGadgets)) != 0),
        static_cast<uint8_t>(
            (mask & static_cast<uint8_t>(Cheat::AllMissions)) != 0),
    };
}

constexpr bool is_main_menu_route(uint32_t definition, uint32_t entry) {
    return definition == main_menu_definition && entry == main_menu_entry;
}

constexpr bool is_installable_menu_row(uint16_t type, uint32_t callback) {
    return (type == stock_load_save_type &&
            callback == stock_load_save_callback) ||
        (type == cheats_type && callback == generic_menu_callback);
}

void register_settings(recomp::config::Config& config);
bool enable_all(recomp::config::Config& config);
bool migrate_fall_damage(recomp::config::Config& config, bool legacy_disabled);
uint8_t selected_mask();
bool initialize(uint8_t* rdram);
bool synchronize(uint8_t* rdram, bool force);

}
