#ifndef TWINE_QOL_HPP
#define TWINE_QOL_HPP

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string_view>

namespace recomp::config {
class Config;
}

namespace twine::qol {

inline constexpr char config_id[] = "gameplay";
inline constexpr char grapple_mode_key[] = "grapple_mode";
inline constexpr char grapple_default_migration_key[] =
    "grapple_pull_default_v2";
inline constexpr char weapon_selection_key[] = "weapon_selection";
inline constexpr char gadget_selection_key[] = "gadget_selection";
inline constexpr char civilian_health_key[] = "civilian_health";
inline constexpr char invincible_npcs_key[] = "invincible_npcs";
inline constexpr char sprint_key[] = "sprint";
inline constexpr char double_darts_key[] = "double_darts";
inline constexpr char health_regeneration_key[] = "health_regeneration";
inline constexpr char remember_weapon_modes_key[] = "remember_weapon_modes";
inline constexpr char fall_damage_key[] = "fall_damage";
inline constexpr char legacy_bonuses_key[] = "single_player_bonuses";

enum class GrappleMode : uint8_t { Classic, ModernPull };
enum class SelectionMode : uint8_t { Classic, Radial };
enum class CivilianHealthMode : uint8_t { Original, Double };
enum class NpcProtectionMode : uint8_t { Off, On };
enum class SprintMode : uint8_t { Off, On };
enum class DartAmmoMode : uint8_t { Original, Double };
enum class HealthRegenerationMode : uint8_t { Off, On };
enum class WeaponModeMemory : uint8_t { Off, On };
enum class FallDamageMode : uint8_t { Original, Off };
enum class LegacyBonusesMode : uint8_t { OriginalBehaviour, Enabled };

inline constexpr GrappleMode default_grapple_mode = GrappleMode::ModernPull;
inline constexpr SprintMode default_sprint_mode = SprintMode::On;

constexpr bool should_migrate_grapple_default(bool migration_complete) {
    return !migration_complete;
}

struct Settings {
    GrappleMode grapple = default_grapple_mode;
    SelectionMode weapons = SelectionMode::Classic;
    SelectionMode gadgets = SelectionMode::Classic;
    CivilianHealthMode civilian_health = CivilianHealthMode::Original;
    NpcProtectionMode npc_protection = NpcProtectionMode::Off;
    SprintMode sprint = default_sprint_mode;
    DartAmmoMode darts = DartAmmoMode::Original;
    HealthRegenerationMode health_regeneration = HealthRegenerationMode::Off;
    WeaponModeMemory weapon_modes = WeaponModeMemory::On;
};

inline bool setting_key_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        const char a = left[i] >= 'A' && left[i] <= 'Z'
            ? static_cast<char>(left[i] - 'A' + 'a') : left[i];
        const char b = right[i] >= 'A' && right[i] <= 'Z'
            ? static_cast<char>(right[i] - 'A' + 'a') : right[i];
        if (a != b) {
            return false;
        }
    }
    return true;
}

inline uint32_t decode_binary_setting(
    std::string_view value,
    std::string_view original_key,
    std::string_view enhanced_key,
    uint32_t default_value = 0U
) {
    if (setting_key_equal(value, original_key)) {
        return 0;
    }
    if (setting_key_equal(value, enhanced_key)) {
        return 1;
    }
    return default_value;
}

void register_settings(recomp::config::Config& config);
bool apply_default_migrations(recomp::config::Config& config);
Settings settings();
bool legacy_bonuses_enabled();
bool legacy_fall_damage_disabled();

enum class InputContext : uint8_t {
    Gameplay,
    Scope,
    Grapple,
    Radial,
    Pause,
    Cutscene,
    Loading,
};

constexpr InputContext higher_priority(InputContext left, InputContext right) {
    return static_cast<uint8_t>(left) >= static_cast<uint8_t>(right)
        ? left : right;
}

enum class SemanticAction : uint16_t {
    WeaponSelect = 1U << 0,
    GadgetSelect = 1U << 1,
    Cancel = 1U << 2,
    Confirm = 1U << 3,
    Scope = 1U << 4,
    ZoomIn = 1U << 5,
    ZoomOut = 1U << 6,
    GrappleFire = 1U << 7,
    GrappleCancel = 1U << 8,
};

constexpr uint16_t action_bit(SemanticAction action) {
    return static_cast<uint16_t>(action);
}

enum class PressEvent : uint8_t { None, Tap, Hold };

class TapHold {
public:
    static constexpr uint16_t default_hold_frames = 12;

    explicit TapHold(uint16_t hold_frames = default_hold_frames)
        : hold_frames_(std::max<uint16_t>(hold_frames, 1)) {}

    PressEvent update(bool down) {
        if (down) {
            was_down_ = true;
            if (frames_ != std::numeric_limits<uint16_t>::max()) {
                ++frames_;
            }
            if (!hold_sent_ && frames_ >= hold_frames_) {
                hold_sent_ = true;
                return PressEvent::Hold;
            }
            return PressEvent::None;
        }

        const PressEvent result = was_down_ && !hold_sent_
            ? PressEvent::Tap : PressEvent::None;
        reset();
        return result;
    }

    void reset() {
        frames_ = 0;
        was_down_ = false;
        hold_sent_ = false;
    }

private:
    uint16_t hold_frames_;
    uint16_t frames_ = 0;
    bool was_down_ = false;
    bool hold_sent_ = false;
};

struct SemanticFrame {
    InputContext context = InputContext::Gameplay;
    uint16_t pressed = 0;
    uint16_t held = 0;
    uint16_t consumed = 0;
    float direction_x = 0.0f;
    float direction_y = 0.0f;

    bool take(InputContext owner, SemanticAction action, bool require_press) {
        const uint16_t bit = action_bit(action);
        const uint16_t available = require_press ? pressed : held;
        if (context != owner || (available & bit) == 0 || (consumed & bit) != 0) {
            return false;
        }
        consumed |= bit;
        return true;
    }
};

enum class LifecycleEvent : uint8_t {
    LevelLoad,
    CheckpointRestore,
    Restart,
    Cutscene,
    Death,
    Pause,
    MultiplayerTransition,
    SettingsChanged,
    Shutdown,
    Frontend,
};

class LifecycleState {
public:
    uint64_t epoch() const { return epoch_; }
    uint64_t notify(LifecycleEvent) { return ++epoch_; }
    bool changed(uint64_t& observed) const {
        if (observed == epoch_) {
            return false;
        }
        observed = epoch_;
        return true;
    }

private:
    uint64_t epoch_ = 1;
};

class AttractSession {
public:
    struct Snapshot { uint32_t mission; bool observed, active, frontend; };
    Snapshot capture() const { return {saved_mission_, observed_, active_, frontend_}; }
    void restore_snapshot(Snapshot saved) noexcept {
        saved_mission_ = saved.mission; observed_ = saved.observed;
        active_ = saved.active; frontend_ = saved.frontend;
    }
    void observe(bool frontend, bool demo, uint32_t active_mission) {
        if (active_) {
            return;
        }
        if (demo) {
            active_ = observed_;
        }
        else if (!frontend) {
            frontend_ = false;
        }
        else if (!frontend_) {
            saved_mission_ = active_mission;
            observed_ = true;
            frontend_ = true;
        }
    }

    bool restore(uint32_t& active_mission) {
        if (!active_) {
            return false;
        }
        active_mission = saved_mission_;
        active_ = false;
        return true;
    }

private:
    uint32_t saved_mission_ = 0;
    bool observed_ = false;
    bool active_ = false;
    bool frontend_ = false;
};

void notify_lifecycle(LifecycleEvent event);
uint64_t lifecycle_epoch();

uint64_t inventory_epoch();

enum class DirtyCategory : uint32_t {
    None = 0,
    Campaign = 1U << 0,
    Objectives = 1U << 1,
    Unlocks = 1U << 2,
    Bonuses = 1U << 3,
    Preferences = 1U << 4,
};

constexpr DirtyCategory operator|(DirtyCategory left, DirtyCategory right) {
    return static_cast<DirtyCategory>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

enum class ProgressEvent : uint8_t {
    MissionComplete,
    ObjectiveComplete,
    DifficultyRecord,
    UnlockChanged,
    BonusChanged,
    LegacyMigration,
};

struct ProgressCommit {
    ProgressEvent event;
    DirtyCategory dirty;
    uint64_t transaction;
};

class ProgressCommitGate {
public:
    bool accept(const ProgressCommit& commit) {
        if (commit.dirty == DirtyCategory::None || commit.transaction == 0 ||
            commit.transaction == last_transaction_) {
            return false;
        }
        last_transaction_ = commit.transaction;
        return true;
    }

private:
    uint64_t last_transaction_ = 0;
};

struct UiRect {
    float left;
    float top;
    float right;
    float bottom;

    float width() const { return right - left; }
    float height() const { return bottom - top; }
    float center_x() const { return (left + right) * 0.5f; }
    float center_y() const { return (top + bottom) * 0.5f; }
};

inline UiRect safe_rect(float width, float height, float margin_fraction = 0.05f) {
    const float margin = std::clamp(margin_fraction, 0.0f, 0.25f);
    return {
        width * margin,
        height * margin,
        width * (1.0f - margin),
        height * (1.0f - margin),
    };
}

}

#endif
