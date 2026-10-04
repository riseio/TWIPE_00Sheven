#include "twine_qol.hpp"
#include "modern_grapple.hpp"

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "librecomp/config.hpp"
#include "recomp.h"
#include "twine_recomp.h"

namespace twine::qol {
namespace {

std::array<std::atomic<uint8_t>, 11> setting_values{};
std::atomic<uint64_t> lifecycle{1};
std::atomic<uint64_t> inventory_lifecycle{1};

uint32_t parse_setting(
    const nlohmann::json& json,
    std::string_view original_key,
    std::string_view enhanced_key,
    uint32_t default_value
) {
    if (json.is_boolean()) {
        return json.get<bool>() ? 1U : 0U;
    }
    if (!json.is_string()) {
        return default_value;
    }
    return decode_binary_setting(
        json.get_ref<const std::string&>(), original_key, enhanced_key, default_value);
}

template <typename Mode>
Mode load_mode(size_t index) {
    return static_cast<Mode>(
        setting_values[index].load(std::memory_order_relaxed));
}

void add_setting(
    recomp::config::Config& config,
    size_t index,
    const char* key,
    const char* name,
    const char* description,
    const char* original_key,
    const char* original_name,
    const char* enhanced_key,
    const char* enhanced_name,
    uint32_t default_value = 0U,
    bool hidden = false
) {
    default_value = default_value <= 1U ? default_value : 0U;
    setting_values[index].store(
        static_cast<uint8_t>(default_value),
        std::memory_order_relaxed);
    config.add_enum_option(
        key,
        name,
        description,
        {
            {0U, original_key, original_name},
            {1U, enhanced_key, enhanced_name},
        },
        default_value,
        hidden);
    config.on_json_parse_option(
        key,
        [original_key, enhanced_key, default_value](const nlohmann::json& json) {

            return recomp::config::ConfigValueVariant{
                parse_setting(json, original_key, enhanced_key, default_value)};
        });
    config.add_option_change_callback(
        key,
        [index, key](recomp::config::ConfigValueVariant value,
                recomp::config::ConfigValueVariant,
                recomp::config::OptionChangeContext context) {
            const uint32_t raw = std::get<uint32_t>(value);
            setting_values[index].store(
                static_cast<uint8_t>(raw <= 1U ? raw : 0U),
                std::memory_order_relaxed);
            if (context == recomp::config::OptionChangeContext::Permanent) {
                notify_lifecycle(LifecycleEvent::SettingsChanged);
            }

        });
}

}

void register_settings(recomp::config::Config& config) {
    add_setting(
        config, 0, grapple_mode_key, "Grapple",
        "Classic preserves the original gadget. Modern Pull adds physical pull traversal while keeping scripted grapple interactions.",
        "classic", "Classic", "modern_pull", "Modern Pull",
        static_cast<uint32_t>(default_grapple_mode));
    add_setting(
        config, 1, weapon_selection_key, "Weapon Selection",
        "Classic cycles weapons normally. Radial opens a hold-to-select weapon wheel; a tap still cycles.",
        "classic", "Classic", "radial", "Radial");
    add_setting(
        config, 2, gadget_selection_key, "Gadget Selection",
        "Classic cycles gadgets normally. Radial opens a hold-to-select gadget wheel; a tap still cycles.",
        "classic", "Classic", "radial", "Radial");
    add_setting(
        config, 3, civilian_health_key, "Civilian Health",
        "Original preserves civilian durability. 2x doubles civilian starting health without changing enemies.",
        "original", "Original", "2x", "2x");
    add_setting(
        config, 10, remember_weapon_modes_key, "Remember Weapon Modes",
        "Keep each weapon's suppressor and firing mode when switching weapons or gadgets, until you change it with Cycle Mode.",
        "off", "Off", "on", "On", 1U);
    add_setting(
        config, 6, invincible_npcs_key, "Invincible Friendly NPCs",
        "Prevents damage to friendly NPCs and civilians, including mission failure from shooting them. Enemies and scripted story events are unchanged.",
        "off", "Off", "on", "On");
    add_setting(
        config, 7, sprint_key, "Hold to Sprint",
        "Hold Sprint (Left Shift) or Action / Interact (F / controller X) while moving forward to run at 1.75 times normal speed on foot. Interactions and reload keep priority. No boost while aiming, crouching, swimming or grappling.",
        "off", "Off", "on", "On",
        static_cast<uint32_t>(default_sprint_mode));
    add_setting(
        config, 8, double_darts_key, "Double Watch Darts",
        "Doubles Watch Dart ammunition grants and capacity. Takes effect on the next mission or restart.",
        "original", "Original", "double", "Double");
    add_setting(
        config, 9, health_regeneration_key, "Regenerating Health",
        "Recover 10% of maximum health per second after five seconds without damage. Armour does not regenerate. Single-player only.",
        "off", "Off", "on", "On");
    add_setting(
        config, 4, fall_damage_key, "Legacy Fall Damage",
        "Compatibility value migrated into Cheats. Not an active QoL option.",
        "original", "Original", "off", "Off", 0U, true);

    add_setting(
        config, 5, legacy_bonuses_key, "Legacy Single-Player Bonuses",
        "One-time compatibility value for the replaced bonuses preference.",
        "original_behaviour", "Original Behaviour", "enabled", "Enabled",
        0U,
        true);
    config.add_bool_option(
        grapple_default_migration_key,
        "Grapple Default Migration",
        "Tracks the one-time Modern Pull default migration.",
        false,
        true);
}

bool apply_default_migrations(recomp::config::Config& config) {
    const bool migration_complete = std::get<bool>(
        config.get_option_value(grapple_default_migration_key));
    if (!should_migrate_grapple_default(migration_complete)) {
        return false;
    }
    config.update_option_value(
        grapple_mode_key,
        static_cast<uint32_t>(default_grapple_mode));
    config.set_option_value(grapple_default_migration_key, true);
    config.save_config();

    return true;
}

Settings settings() {
    return {
        load_mode<GrappleMode>(0),
        load_mode<SelectionMode>(1),
        load_mode<SelectionMode>(2),
        load_mode<CivilianHealthMode>(3),
        load_mode<NpcProtectionMode>(6),
        load_mode<SprintMode>(7),
        load_mode<DartAmmoMode>(8),
        load_mode<HealthRegenerationMode>(9),
        load_mode<WeaponModeMemory>(10),
    };
}

bool legacy_bonuses_enabled() {
    return load_mode<LegacyBonusesMode>(5) == LegacyBonusesMode::Enabled;
}

bool legacy_fall_damage_disabled() {
    return load_mode<FallDamageMode>(4) == FallDamageMode::Off;
}

void notify_lifecycle(LifecycleEvent event) {
    if (event != LifecycleEvent::SettingsChanged) {
        inventory_lifecycle.fetch_add(1, std::memory_order_release);
    }
    lifecycle.fetch_add(1, std::memory_order_release);
}

uint64_t lifecycle_epoch() {
    return lifecycle.load(std::memory_order_acquire);
}

uint64_t inventory_epoch() {
    return inventory_lifecycle.load(std::memory_order_acquire);
}

}

extern "C" void twine_invalidate_transient_state(
    uint8_t* rdram,
    recomp_context* ctx
) {
    (void)ctx;
    twine::qol::notify_lifecycle(twine::qol::LifecycleEvent::LevelLoad);
    twine::grapple::invalidate_presentation();
}
