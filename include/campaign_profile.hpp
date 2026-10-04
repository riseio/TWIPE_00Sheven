#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace twine::campaign {

inline constexpr uint32_t schema_version = 2;
inline constexpr uint32_t settings_revision = 1;
inline constexpr size_t payload_size = 0x200;

struct Profile {
    uint64_t generation = 0;
    uint64_t selected_bonuses = 0;
    uint32_t settings_revision = campaign::settings_revision;
    std::array<uint8_t, payload_size> payload{};
};

uint16_t legacy_checksum(std::span<const uint8_t> bytes);
bool validate_legacy_payload(std::span<const uint8_t> bytes);
void update_legacy_checksum(std::span<uint8_t, payload_size> bytes);

class Store {
public:
    explicit Store(std::filesystem::path directory);

    bool initialize(const std::optional<Profile>& legacy = std::nullopt);
    bool has_profile() const { return profile_.has_value(); }
    const Profile& profile() const { return *profile_; }
    bool commit(std::span<const uint8_t, payload_size> payload,
                std::optional<uint32_t> revision = std::nullopt);
    bool set_selected_bonuses(uint64_t selected_bonuses);
    bool reset(std::span<const uint8_t, payload_size> payload);
    bool flush();
    bool dirty() const { return dirty_; }
    void restore_session(const std::optional<Profile>& profile) noexcept {
        const uint64_t generation = profile_ ? profile_->generation : 0;
        profile_ = profile;
        if (profile_ && profile_->generation < generation) profile_->generation = generation;
        dirty_ = false;
    }

    const std::filesystem::path& path() const { return path_; }

private:
    bool persist();

    std::filesystem::path path_;
    std::optional<Profile> profile_;
    bool dirty_ = false;
};

bool initialize(const std::filesystem::path& config_path);
void shutdown();
bool installed();
bool request_reset();
uint64_t selected_bonuses();
bool set_selected_bonuses(uint64_t value);
uint64_t generation();
bool dirty();
bool runtime_payload_matches(const uint8_t* rdram);

bool restore_state_options(uint8_t* rdram) noexcept;

}
