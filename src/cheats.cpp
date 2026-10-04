#include "cheats.hpp"

#include <atomic>
#include <variant>

#include "librecomp/config.hpp"
#include "recomp.h"
#include "twine_recomp.h"

namespace twine::cheats {
namespace {

constexpr uint32_t invulnerability_address = 0x80112F68U;
constexpr uint32_t all_weapons_address = 0x80112F6CU;
constexpr uint32_t all_gadgets_address = 0x80112F6DU;
constexpr uint32_t all_missions_address = 0x80112F6EU;

std::atomic<uint8_t> active_mask{0};
std::atomic_bool synchronization_pending{true};

void set_enabled(Cheat cheat, bool enabled) {
    uint8_t observed = active_mask.load(std::memory_order_relaxed);
    uint8_t updated = with_enabled(observed, cheat, enabled);
    while (!active_mask.compare_exchange_weak(
            observed,
            updated,
            std::memory_order_release,
            std::memory_order_relaxed)) {
        updated = with_enabled(observed, cheat, enabled);
    }
    synchronization_pending.store(true, std::memory_order_release);
}

void add_setting(
    recomp::config::Config& config,
    Cheat cheat,
    const char* key,
    const char* name,
    const char* description
) {
    config.add_bool_option(key, name, description, false);
    config.add_option_change_callback(
        key,
        [cheat, key](
            recomp::config::ConfigValueVariant value,
            recomp::config::ConfigValueVariant,
            recomp::config::OptionChangeContext context
        ) {
            const bool enabled = std::get<bool>(value);
            set_enabled(cheat, enabled);

        });
}

void write_native_state(uint8_t* rdram, uint8_t mask) {
    const NativeState state = native_state(mask);
    TWINE_MEM_W(0, invulnerability_address) = state.invulnerability;
    TWINE_MEM_B(0, all_weapons_address) = state.all_weapons;
    TWINE_MEM_B(0, all_gadgets_address) = state.all_gadgets;
    TWINE_MEM_B(0, all_missions_address) = state.all_missions;
}

}

void register_settings(recomp::config::Config& config) {

    active_mask.store(0, std::memory_order_release);
    synchronization_pending.store(true, std::memory_order_release);
    add_setting(
        config,
        Cheat::Invulnerability,
        invulnerability_key,
        "Invulnerability",
        "Prevents the player from taking damage in single-player missions.");
    add_setting(
        config, Cheat::InfiniteOxygen, infinite_oxygen_key,
        "Infinite O2",
        "Keeps the player's oxygen full while underwater in single-player missions.");
    add_setting(
        config, Cheat::NoFallDamage, no_fall_damage_key,
        "No Fall Damage",
        "Prevents landing-impact damage in single-player missions without disabling combat damage or drowning.");
    add_setting(
        config,
        Cheat::AllWeapons,
        all_weapons_key,
        "All Weapons",
        "Adds every weapon when a single-player mission starts.");
    add_setting(
        config,
        Cheat::AllGadgets,
        all_gadgets_key,
        "All Gadgets",
        "Adds every gadget when a single-player mission starts.");
    add_setting(
        config,
        Cheat::AllMissions,
        all_missions_key,
        "All Missions",
        "Unlocks every mission and difficulty in Mission Selection.");
    config.add_bool_option(
        fall_damage_migration_key, "Fall Damage Migration",
        "Tracks migration of the old QoL Fall Damage preference.", false, true);
}

bool enable_all(recomp::config::Config& config) {

    config.update_option_value(invulnerability_key, true);
    config.update_option_value(all_weapons_key, true);
    config.update_option_value(all_gadgets_key, true);
    config.update_option_value(all_missions_key, true);
    return config.save_config();
}

bool migrate_fall_damage(recomp::config::Config& config, bool legacy_disabled) {
    if (std::get<bool>(config.get_option_value(fall_damage_migration_key))) {
        return true;
    }
    if (legacy_disabled) {
        config.update_option_value(no_fall_damage_key, true);
    }
    config.set_option_value(fall_damage_migration_key, true);
    const bool saved = config.save_config();
    if (!saved) {
        config.set_option_value(fall_damage_migration_key, false);
    }

    return saved;
}

uint8_t selected_mask() {
    return active_mask.load(std::memory_order_acquire);
}

bool synchronize(uint8_t* rdram, bool force) {
    if (rdram == nullptr) {
        return false;
    }
    const bool pending = synchronization_pending.exchange(
        false, std::memory_order_acq_rel);
    if (!force && !pending) {
        return false;
    }

    const uint8_t mask = selected_mask();
    write_native_state(rdram, mask);

    return true;
}

bool initialize(uint8_t* rdram) {
    if (rdram == nullptr) {
        return false;
    }

    const uint16_t type = TWINE_MEM_HU(0, main_menu_entry);
    const uint32_t callback = static_cast<uint32_t>(
        TWINE_MEM_W(4, main_menu_entry));
    if (!is_installable_menu_row(type, callback)) {

        return false;
    }

    TWINE_MEM_H(0, main_menu_entry) = cheats_type;
    TWINE_MEM_W(4, main_menu_entry) = generic_menu_callback;

    synchronize(rdram, true);
    return true;
}

}

extern "C" void twine_sync_cheats(uint8_t* rdram, recomp_context*) {
    twine::cheats::synchronize(rdram, false);
}

extern "C" void twine_restore_cheats(uint8_t* rdram, recomp_context*) {
    twine::cheats::synchronize(rdram, true);
}
