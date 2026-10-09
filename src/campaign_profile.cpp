#include "save_state_owner.hpp"
#include "campaign_profile.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "controller_pak.hpp"
#include "recomp.h"

namespace twine::campaign {
namespace {

constexpr std::array<uint8_t, 8> magic{'T', 'W', 'N', 'Q', 'O', 'L', '1', 0};
constexpr size_t header_size = 48;
constexpr uint16_t twine_company = 0x3639;
constexpr uint32_t twine_game_code = 0x4E4F3745;
constexpr size_t pak_directory_entries = 16;

uint32_t read_u32(const uint8_t* bytes) {
    return uint32_t{bytes[0]} |
        (uint32_t{bytes[1]} << 8) |
        (uint32_t{bytes[2]} << 16) |
        (uint32_t{bytes[3]} << 24);
}

uint64_t read_u64(const uint8_t* bytes) {
    return uint64_t{read_u32(bytes)} |
        (uint64_t{read_u32(bytes + 4)} << 32);
}

void write_u32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

void write_u64(uint8_t* bytes, uint64_t value) {
    write_u32(bytes, static_cast<uint32_t>(value));
    write_u32(bytes + 4, static_cast<uint32_t>(value >> 32));
}

uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xFFFFFFFFU;
    for (const uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U &
                (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

std::vector<uint8_t> serialize(const Profile& profile) {
    std::vector<uint8_t> bytes(header_size + payload_size, 0);
    std::copy(magic.begin(), magic.end(), bytes.begin());
    write_u32(bytes.data() + 8, schema_version);
    write_u32(bytes.data() + 12, static_cast<uint32_t>(header_size));
    write_u64(bytes.data() + 16, profile.generation);
    write_u32(bytes.data() + 24, static_cast<uint32_t>(payload_size));
    write_u32(bytes.data() + 28, crc32(profile.payload));
    write_u64(bytes.data() + 32, profile.selected_bonuses);
    write_u32(bytes.data() + 40, crc32(std::span(bytes).first(40)));
    std::copy(profile.payload.begin(), profile.payload.end(),
        bytes.begin() + header_size);
    return bytes;
}

std::optional<Profile> deserialize(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() !=
            static_cast<std::streamoff>(header_size + payload_size)) {
        return std::nullopt;
    }
    input.seekg(0);
    std::array<uint8_t, header_size + payload_size> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!input || !std::equal(magic.begin(), magic.end(), bytes.begin()) ||
        read_u32(bytes.data() + 8) != schema_version ||
        read_u32(bytes.data() + 12) != header_size ||
        read_u32(bytes.data() + 24) != payload_size ||
        read_u32(bytes.data() + 44) != 0 ||
        read_u32(bytes.data() + 40) !=
            crc32(std::span(bytes).first(40))) {
        return std::nullopt;
    }

    Profile result;
    result.generation = read_u64(bytes.data() + 16);
    result.selected_bonuses = read_u64(bytes.data() + 32);
    std::copy(bytes.begin() + header_size, bytes.end(), result.payload.begin());
    if (result.generation == 0 ||
        read_u32(bytes.data() + 28) != crc32(result.payload) ||
        !validate_legacy_payload(result.payload)) {
        return std::nullopt;
    }
    return result;
}

bool durable_flush(const std::filesystem::path& path) {
#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return ok;
#else
    const int file = ::open(path.c_str(), O_WRONLY);
    if (file < 0) {
        return false;
    }
    const bool ok = ::fsync(file) == 0;
    ::close(file);
    return ok;
#endif
}

bool durable_flush_directory(const std::filesystem::path& path) {
#ifdef _WIN32

    return true;
#else
    const int directory = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) {
        return false;
    }
    const bool ok = ::fsync(directory) == 0;
    ::close(directory);
    return ok;
#endif
}

bool atomic_replace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
) {
#ifdef _WIN32
    return MoveFileExW(
        source.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

bool write_profile(const std::filesystem::path& path, const Profile& profile) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += ".temp";
    const std::vector<uint8_t> bytes = serialize(profile);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.flush();
        if (!output) {
            return false;
        }
    }
    if (!durable_flush(temporary)) {
        return false;
    }
    if (!durable_flush_directory(path.parent_path())) {
        return false;
    }
    const auto verified = deserialize(temporary);
    if (!verified || verified->generation != profile.generation ||
        verified->payload != profile.payload ||
        verified->selected_bonuses != profile.selected_bonuses) {
        return false;
    }

    if (std::filesystem::exists(path, error) && !error) {
        std::filesystem::path backup = path;
        backup += ".bak";
        std::filesystem::path backup_temporary = backup;
        backup_temporary += ".temp";
        std::filesystem::copy_file(
            path, backup_temporary,
            std::filesystem::copy_options::overwrite_existing, error);
        if (error || !durable_flush(backup_temporary) ||
            !atomic_replace(backup_temporary, backup) ||
            !durable_flush_directory(path.parent_path())) {
            return false;
        }
    }

    if (!atomic_replace(temporary, path) ||
            !durable_flush_directory(path.parent_path())) {
        return false;
    }
    const auto promoted = deserialize(path);
    return promoted && promoted->generation == profile.generation &&
        promoted->payload == profile.payload &&
        promoted->selected_bonuses == profile.selected_bonuses;
}

std::optional<Profile> newest_canonical(const std::filesystem::path& path) {
    std::array<std::filesystem::path, 3> paths{path, path, path};
    paths[1] += ".temp";
    paths[2] += ".bak";

    std::optional<Profile> newest;
    for (const auto& candidate_path : paths) {
        const auto candidate = deserialize(candidate_path);
        if (candidate && (!newest ||
                candidate->generation > newest->generation)) {
            newest = candidate;
        }
    }
    return newest;
}

std::optional<Profile> discover_legacy(const std::filesystem::path& config_path) {
    struct Candidate {
        Profile profile;
        std::filesystem::file_time_type modified;
        size_t port;
        size_t file;
    };
    std::vector<Candidate> candidates;
    for (size_t port = 0; port < 4; ++port) {
        const std::filesystem::path path = config_path / "paks" /
            ("controller-" + std::to_string(port + 1) + ".mpk");
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            continue;
        }
        ControllerPak pak(path, static_cast<uint32_t>(port + 1));
        if (pak.load_existing_read_only() != pfs::Ok) {
            continue;
        }
        const auto modified = std::filesystem::last_write_time(path, error);
        for (size_t file = 0; file < pak_directory_entries; ++file) {
            PakFileState state{};
            if (pak.file_state(static_cast<int32_t>(file), state) != pfs::Ok ||
                state.company_code != twine_company ||
                state.game_code != twine_game_code ||
                state.file_size != payload_size) {
                continue;
            }
            Profile profile;
            profile.generation = 1;
            if (pak.read_write(
                    static_cast<int32_t>(file), false, 0, profile.payload) == pfs::Ok &&
                validate_legacy_payload(profile.payload)) {
                candidates.push_back({profile, modified, port, file});
            }
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    const auto selected = std::max_element(
        candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            if (left.modified != right.modified) {
                return left.modified < right.modified;
            }
            if (left.port != right.port) {
                return left.port > right.port;
            }
            return left.file > right.file;
        });
    (void)(candidates.size() > 1);
    return selected->profile;
}

std::mutex runtime_mutex;
std::unique_ptr<Store> runtime_store;
std::optional<std::array<uint8_t, payload_size>> runtime_fresh_payload;
bool runtime_installed = false;
bool runtime_initializing = false;
bool runtime_apply_pending = false;

}

uint16_t legacy_checksum(std::span<const uint8_t> bytes) {
    uint16_t crc = 0xFFFFU;
    for (const uint8_t byte : bytes) {
        uint8_t value = byte;
        for (int bit = 0; bit < 8; ++bit) {
            const bool different = (crc & 1U) != (value & 1U);
            crc >>= 1;
            if (different) {
                crc ^= 0x8408U;
            }
            value >>= 1;
        }
    }
    crc = static_cast<uint16_t>(~crc);
    return static_cast<uint16_t>((crc << 8) | (crc >> 8));
}

bool validate_legacy_payload(std::span<const uint8_t> bytes) {
    if (bytes.size() != payload_size) {
        return false;
    }
    const uint16_t stored = static_cast<uint16_t>(
        (uint16_t{bytes[0]} << 8) | bytes[1]);
    return stored == legacy_checksum(bytes.subspan(2));
}

void update_legacy_checksum(std::span<uint8_t, payload_size> bytes) {
    const uint16_t checksum = legacy_checksum(bytes.subspan(2));
    bytes[0] = static_cast<uint8_t>(checksum >> 8);
    bytes[1] = static_cast<uint8_t>(checksum);
}

Store::Store(std::filesystem::path directory)
    : path_(std::move(directory) / "campaign.profile") {}

bool Store::initialize(const std::optional<Profile>& legacy) {
    profile_ = newest_canonical(path_);
    if (profile_) {
        dirty_ = false;
        return true;
    }
    if (!legacy) {
        dirty_ = false;
        return true;
    }
    profile_ = legacy;
    profile_->generation = 1;
    dirty_ = true;
    return persist();
}

bool Store::commit(std::span<const uint8_t, payload_size> payload) {
    std::array<uint8_t, payload_size> checked;
    std::copy(payload.begin(), payload.end(), checked.begin());
    update_legacy_checksum(checked);
    if (profile_ && profile_->payload == checked && !dirty_) {
        return true;
    }
    Profile next = profile_.value_or(Profile{});
    ++next.generation;
    next.payload = checked;
    profile_ = next;
    dirty_ = true;
    return persist();
}

bool Store::set_selected_bonuses(uint64_t selected_bonuses) {
    if (!profile_) {
        return false;
    }
    if (profile_->selected_bonuses == selected_bonuses && !dirty_) {
        return true;
    }
    ++profile_->generation;
    profile_->selected_bonuses = selected_bonuses;
    dirty_ = true;
    return persist();
}

bool Store::reset(std::span<const uint8_t, payload_size> payload) {
    Profile reset;
    reset.generation = profile_ ? profile_->generation + 1 : 1;
    std::copy(payload.begin(), payload.end(), reset.payload.begin());
    update_legacy_checksum(reset.payload);
    profile_ = reset;
    dirty_ = true;
    return persist();
}

bool Store::flush() {
    return !dirty_ || persist();
}

bool Store::persist() {
    if (!profile_ || !write_profile(path_, *profile_)) {
        std::fprintf(stderr, "TWINE_PROFILE error=durable_write_failed\n");
        return false;
    }
    dirty_ = false;
    return true;
}

bool initialize(const std::filesystem::path& config_path) {
    std::lock_guard lock(runtime_mutex);
    runtime_store = std::make_unique<Store>(config_path / "saves");
    runtime_fresh_payload.reset();
    runtime_initializing = false;
    runtime_installed = false;
    runtime_apply_pending = false;
    return runtime_store->initialize(discover_legacy(config_path));
}

void shutdown() {
    std::lock_guard lock(runtime_mutex);
    if (runtime_store && runtime_store->dirty() && !runtime_store->flush()) {
        std::fprintf(
            stderr,
            "TWINE_PROFILE error=shutdown_retry_failed dirty=1\n");

    }
    runtime_store.reset();
    runtime_fresh_payload.reset();
    runtime_installed = false;
    runtime_apply_pending = false;
}

bool installed() {
    std::lock_guard lock(runtime_mutex);
    return runtime_installed;
}

bool request_reset() {
    std::lock_guard lock(runtime_mutex);
    if (!runtime_store || !runtime_fresh_payload ||
        !runtime_store->reset(*runtime_fresh_payload)) {
        return false;
    }
    runtime_installed = false;
    runtime_apply_pending = false;
    return true;
}

uint64_t selected_bonuses() {
    std::lock_guard lock(runtime_mutex);
    return runtime_store && runtime_store->has_profile()
        ? runtime_store->profile().selected_bonuses : 0;
}

bool set_selected_bonuses(uint64_t value) {
    std::lock_guard lock(runtime_mutex);
    return runtime_store && runtime_store->set_selected_bonuses(value);
}

uint64_t generation() {
    std::lock_guard lock(runtime_mutex);
    return runtime_store && runtime_store->has_profile()
        ? runtime_store->profile().generation : 0;
}

bool dirty() {
    std::lock_guard lock(runtime_mutex);
    return runtime_store && runtime_store->dirty();
}

bool runtime_payload_matches(const uint8_t* rdram) {
    constexpr size_t payload_offset = 0xE1110;
    if (rdram == nullptr) {
        return false;
    }
    std::lock_guard lock(runtime_mutex);
    if (!runtime_store || !runtime_store->has_profile()) {
        return false;
    }
    const auto& payload = runtime_store->profile().payload;
    for (size_t offset = 0; offset < payload.size(); ++offset) {
        if (rdram[(payload_offset + offset) ^ 3U] != payload[offset]) {
            return false;
        }
    }
    return true;
}

}

namespace {

constexpr uint32_t rdram_base = 0x80000000U;
constexpr uint32_t campaign_payload_address = 0x800E1110U;
thread_local bool level_completion_pending = false;

uint8_t read_rdram_byte(uint8_t* rdram, size_t offset) {
    return rdram[((campaign_payload_address - rdram_base) + offset) ^ 3U];
}

uint8_t read_rdram_u8(uint8_t* rdram, uint32_t address) {
    return rdram[(address - rdram_base) ^ 3U];
}

void write_rdram_byte(uint8_t* rdram, size_t offset, uint8_t value) {
    rdram[((campaign_payload_address - rdram_base) + offset) ^ 3U] = value;
}

bool restore_uninitialized_player_options(
    std::array<uint8_t, twine::campaign::payload_size>& payload,
    const std::array<uint8_t, twine::campaign::payload_size>& defaults
) {

    constexpr size_t player_count = 4;
    constexpr size_t record_bits = 118;
    constexpr size_t option_bits = 13;
    constexpr size_t autoaim_bit = 3;
    for (size_t player = 0; player < player_count; ++player) {
        for (size_t option = 0; option < option_bits; ++option) {
            const size_t bit = 32 + player * record_bits + option;
            if (option != autoaim_bit && (payload[bit / 8] & (1U << (bit % 8)))) {
                return false;
            }
        }
    }
    bool changed = false;
    for (size_t player = 0; player < player_count; ++player) {
        for (size_t option = 0; option < option_bits; ++option) {
            if (option == autoaim_bit) continue;
            const size_t bit = 32 + player * record_bits + option;
            const uint8_t mask = static_cast<uint8_t>(1U << (bit % 8));
            const uint8_t value = (payload[bit / 8] & ~mask) | (defaults[bit / 8] & mask);
            changed |= payload[bit / 8] != value;
            payload[bit / 8] = value;
        }
    }
    return changed;
}

std::array<uint8_t, twine::campaign::payload_size> capture_payload(
    uint8_t* rdram
) {
    std::array<uint8_t, twine::campaign::payload_size> payload;
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = read_rdram_byte(rdram, i);
    }
    return payload;
}

uint32_t capture_profile(uint8_t* rdram, const char* reason) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    if (!twine::campaign::runtime_store) {
        return 0;
    }
    const uint64_t before = twine::campaign::runtime_store->has_profile()
        ? twine::campaign::runtime_store->profile().generation : 0;
    auto payload = capture_payload(rdram);
    twine::campaign::update_legacy_checksum(payload);
    for (size_t i = 0; i < payload.size(); ++i) {
        write_rdram_byte(rdram, i, payload[i]);
    }
    const bool committed = twine::campaign::runtime_store->commit(payload);
    const uint64_t after = twine::campaign::runtime_store->has_profile()
        ? twine::campaign::runtime_store->profile().generation : 0;
    std::fprintf(
        stderr,
        "TWINE_PROFILE commit=%s result=%s generation_before=%llu "
        "generation_after=%llu dirty=%u\n",
        reason,
        committed ? "durable" : "failed",
        static_cast<unsigned long long>(before),
        static_cast<unsigned long long>(after),
        twine::campaign::runtime_store->dirty() ? 1U : 0U);

    return committed ? 1U : 0U;
}

}

void twine::campaign::restore_state_options(uint8_t* rdram) noexcept {
    std::lock_guard lock(runtime_mutex);
    if (!rdram || !runtime_fresh_payload) return;
    constexpr std::array<uint32_t, 10> offsets{0x88, 0x89, 0x8A, 0x8B, 0x8C,
        0x8D, 0x90, 0x91, 0x8E, 0x8F};
    for (uint32_t player = 0; player < 4; ++player) {
        for (const uint32_t offset : offsets) {
            if (offset != 0x8B && read_rdram_u8(rdram, 0x80115000 + player * 0xA0 + offset)) return;
        }
    }
    for (uint32_t player = 0; player < 4; ++player) {
        size_t bit = 32 + player * 118;
        for (const uint32_t offset : offsets) {
            const size_t width = offset == 0x91 ? 4 : 1;
            uint8_t value = 0;
            for (size_t i = 0; i < width; ++i, ++bit) {
                value |= (((*runtime_fresh_payload)[bit / 8] >> (bit % 8)) & 1U) << i;
            }
            if (offset != 0x8B) rdram[(0x115000 + player * 0xA0 + offset) ^ 3U] = value;
        }
    }
    auto payload = capture_payload(rdram);
    if (restore_uninitialized_player_options(payload, *runtime_fresh_payload)) {
        update_legacy_checksum(payload);
        for (size_t i = 0; i < payload.size(); ++i) write_rdram_byte(rdram, i, payload[i]);
    }
}

extern "C" uint32_t twine_profile_should_commit(uint8_t*, recomp_context*) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    return twine::campaign::runtime_installed &&
        !twine::campaign::runtime_apply_pending &&
        !twine::campaign::runtime_initializing ? 1U : 0U;
}

extern "C" uint32_t twine_profile_capture(
    uint8_t* rdram,
    recomp_context*
) {
    return capture_profile(rdram, "native");
}

extern "C" uint32_t twine_profile_capture_level_completion(
    uint8_t* rdram,
    recomp_context*
) {
    return capture_profile(rdram, "level_completion");
}

extern "C" void twine_profile_begin_defaults(uint8_t*, recomp_context*) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    twine::campaign::runtime_initializing = true;
    twine::campaign::runtime_installed = false;
    twine::campaign::runtime_apply_pending = false;
}

extern "C" void twine_profile_finish_defaults(uint8_t* rdram, recomp_context*) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    if (!twine::campaign::runtime_fresh_payload) {
        auto fresh = capture_payload(rdram);
        twine::campaign::update_legacy_checksum(fresh);
        twine::campaign::runtime_fresh_payload = fresh;
    }
    twine::campaign::runtime_initializing = false;
}

extern "C" uint32_t twine_profile_install(
    uint8_t* rdram,
    recomp_context*
) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    if (!twine::campaign::runtime_store || twine::campaign::runtime_initializing ||
            !twine::campaign::runtime_fresh_payload) {
        return 0;
    }
    const auto& fresh = *twine::campaign::runtime_fresh_payload;
    if (!twine::campaign::runtime_store->has_profile() &&
             !twine::campaign::runtime_store->commit(fresh)) {
        return 0;
    }
    if (!twine::campaign::runtime_installed) {
        auto repaired = twine::campaign::runtime_store->profile().payload;
        if (restore_uninitialized_player_options(repaired, fresh) &&
                !twine::campaign::runtime_store->commit(repaired)) {
            return 0;
        }
    }
    const auto& payload = twine::campaign::runtime_store->profile().payload;
    for (size_t i = 0; i < payload.size(); ++i) {
        write_rdram_byte(rdram, i, payload[i]);
    }
    if (!twine::campaign::runtime_installed) {
        twine::campaign::runtime_apply_pending = true;
    }
    twine::campaign::runtime_installed = true;
    return 1;
}

extern "C" uint32_t twine_profile_take_pending_apply(
    uint8_t*,
    recomp_context*
) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    const bool pending = twine::campaign::runtime_apply_pending;
    twine::campaign::runtime_apply_pending = false;
    return pending ? 1U : 0U;
}

extern "C" void twine_profile_level_completion_begin(
    uint8_t* rdram,
    recomp_context* ctx
) {

    const uint32_t status = ctx == nullptr ? UINT32_MAX :
        static_cast<uint32_t>(ctx->r2);
    const bool installed = twine::campaign::installed();
    level_completion_pending = status == 5U && installed;

}

extern "C" uint32_t twine_profile_take_level_completion(
    uint8_t*,
    recomp_context*
) {
    const bool pending = level_completion_pending;
    level_completion_pending = false;

    return pending ? 1U : 0U;
}

extern "C" uint32_t twine_profile_legacy_load(
    uint8_t*,
    recomp_context* ctx
) {
    std::lock_guard lock(twine::campaign::runtime_mutex);
    if (!twine::campaign::runtime_store) {
        return 0;
    }
    ctx->r2 = 1;
    return 1;
}

extern "C" uint32_t twine_profile_legacy_save(
    uint8_t* rdram,
    recomp_context* ctx
) {
    if (!twine::campaign::installed()) {
        std::lock_guard lock(twine::campaign::runtime_mutex);
        if (!twine::campaign::runtime_store) {
            return 0;
        }
        ctx->r2 = 3;
        return 1;
    }
    if (twine_profile_capture(rdram, ctx) == 0) {
        return 0;
    }
    ctx->r2 = 3;
    return 1;
}

twine::state::Bytes twine::state::capture_profile_session() {
    std::lock_guard lock(campaign::runtime_mutex);
    if (!campaign::runtime_store) throw std::runtime_error("Campaign profile is unavailable");
    if (campaign::runtime_store->dirty() && !campaign::runtime_store->flush())
        throw std::runtime_error("Campaign profile has an unfinished write");
    Writer out; const bool has = campaign::runtime_store->has_profile();
    out.fields(uint32_t(1), has, campaign::runtime_installed, campaign::runtime_apply_pending, level_completion_pending);
    if (has) {
        const auto& profile = campaign::runtime_store->profile();
        out.fields(profile.generation, profile.selected_bonuses); out.blob(profile.payload);
    }
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_profile_session(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid campaign state schema");
    bool has, installed, pending, completion; in.fields(has, installed, pending, completion);
    std::optional<campaign::Profile> profile;
    if (has) {
        profile.emplace(); in.fields(profile->generation, profile->selected_bonuses);
        const auto payload = in.blob(campaign::payload_size);
        if (profile->generation == 0 || payload.size() != campaign::payload_size || !campaign::validate_legacy_payload(payload))
            throw std::runtime_error("Invalid campaign profile state");
        std::copy(payload.begin(), payload.end(), profile->payload.begin());
    }
    if (installed && !has) throw std::runtime_error("Installed campaign state has no profile");
    in.end();
    if (!campaign::runtime_store) throw std::runtime_error("Campaign profile is unavailable");
    if (profile && campaign::runtime_fresh_payload &&
            restore_uninitialized_player_options(profile->payload, *campaign::runtime_fresh_payload)) {
        campaign::update_legacy_checksum(profile->payload);
    }
    return prepared_owner([profile, installed, pending, completion]() noexcept {
        std::lock_guard lock(campaign::runtime_mutex);
        campaign::runtime_store->restore_session(profile);
        campaign::runtime_installed = installed; campaign::runtime_apply_pending = pending;
        level_completion_pending = completion;
    });
}
