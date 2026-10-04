#include "save_state_owner.hpp"
#include "pfs_runtime.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "controller_pak.hpp"
#include "librecomp/game.hpp"
#include "recomp.h"
#include "ultramodern/ultra64.h"

namespace {

constexpr uint32_t rdram_start = 0x80000000U;
constexpr uint32_t rdram_size = 8U * 1024U * 1024U;
constexpr uint32_t pfs_initialized = 1;

std::mutex pak_mutex;
std::filesystem::path pak_directory;
std::array<std::unique_ptr<twine::ControllerPak>, 4> paks;

uint32_t raw_address(gpr address) {
    return static_cast<uint32_t>(address);
}

bool valid_rdram_range(gpr address, size_t size) {
    const uint32_t raw = raw_address(address);
    if (raw < rdram_start || raw >= rdram_start + rdram_size) {
        return false;
    }
    const size_t offset = static_cast<size_t>(raw - rdram_start);
    return size <= rdram_size - offset;
}

uint8_t read_byte(uint8_t* rdram, gpr address, size_t offset = 0) {
    const size_t physical = static_cast<size_t>(raw_address(address) - rdram_start) + offset;
    return rdram[physical ^ 3U];
}

void write_byte(uint8_t* rdram, gpr address, size_t offset, uint8_t value) {
    const size_t physical = static_cast<size_t>(raw_address(address) - rdram_start) + offset;
    rdram[physical ^ 3U] = value;
}

uint32_t read_u32(uint8_t* rdram, gpr address) {
    return (uint32_t{read_byte(rdram, address, 0)} << 24) |
           (uint32_t{read_byte(rdram, address, 1)} << 16) |
           (uint32_t{read_byte(rdram, address, 2)} << 8) |
           uint32_t{read_byte(rdram, address, 3)};
}

void write_u16(uint8_t* rdram, gpr address, uint16_t value) {
    write_byte(rdram, address, 0, static_cast<uint8_t>(value >> 8));
    write_byte(rdram, address, 1, static_cast<uint8_t>(value));
}

void write_u32(uint8_t* rdram, gpr address, uint32_t value) {
    write_byte(rdram, address, 0, static_cast<uint8_t>(value >> 24));
    write_byte(rdram, address, 1, static_cast<uint8_t>(value >> 16));
    write_byte(rdram, address, 2, static_cast<uint8_t>(value >> 8));
    write_byte(rdram, address, 3, static_cast<uint8_t>(value));
}

gpr stack_argument(uint8_t* rdram, const recomp_context* context, size_t index) {
    const gpr address = context->r29 + 0x10 + index * sizeof(uint32_t);
    if (!valid_rdram_range(address, sizeof(uint32_t))) {
        return 0;
    }
    return static_cast<gpr>(static_cast<int64_t>(static_cast<int32_t>(read_u32(rdram, address))));
}

OSPfs* get_pfs(uint8_t* rdram, gpr address) {
    if (!valid_rdram_range(address, sizeof(OSPfs))) {
        return nullptr;
    }
    return reinterpret_cast<OSPfs*>(
        rdram + static_cast<size_t>(raw_address(address) - rdram_start));
}

template <size_t Size>
bool read_array(uint8_t* rdram, gpr address, std::array<uint8_t, Size>& output) {
    if (!valid_rdram_range(address, Size)) {
        return false;
    }
    for (size_t index = 0; index < Size; ++index) {
        output[index] = read_byte(rdram, address, index);
    }
    return true;
}

void ensure_directory() {
    if (pak_directory.empty()) {
        pak_directory = recomp::get_config_path() / "paks";
    }
}

std::pair<twine::ControllerPak*, int> pak_for_channel(int32_t channel) {
    if (channel < 0 || channel >= static_cast<int32_t>(paks.size())) {
        return {nullptr, twine::pfs::Invalid};
    }

    ensure_directory();
    auto& pak = paks[static_cast<size_t>(channel)];
    if (!pak) {
        pak = std::make_unique<twine::ControllerPak>(
            pak_directory / ("controller-" + std::to_string(channel + 1) + ".mpk"),
            static_cast<uint32_t>(channel + 1));
    }
    const int result = pak->load_or_create();
    return {result == twine::pfs::Ok ? pak.get() : nullptr, result};
}

std::pair<twine::ControllerPak*, int> pak_for_pfs(uint8_t* rdram, gpr address) {
    OSPfs* pfs = get_pfs(rdram, address);
    if (pfs == nullptr || (pfs->status & pfs_initialized) == 0) {
        return {nullptr, twine::pfs::Invalid};
    }
    return pak_for_channel(pfs->channel);
}

void return_error(recomp_context* context, int result) {
    context->r2 = static_cast<gpr>(static_cast<int64_t>(static_cast<int32_t>(result)));
}

}

void twine::pfs_runtime::initialize(const std::filesystem::path& config_path) {
    std::lock_guard lock(pak_mutex);
    const std::filesystem::path requested = config_path / "paks";
    if (pak_directory != requested) {
        pak_directory = requested;
        paks = {};
    }
}

extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    OSPfs* pfs = get_pfs(rdram, context->r5);
    const int32_t channel = static_cast<int32_t>(context->r6);
    if (pfs == nullptr) {
        return_error(context, twine::pfs::Invalid);
        return;
    }

    pfs->queue = static_cast<int32_t>(context->r4);
    pfs->channel = channel;
    pfs->status = 0;
    pfs->activebank = 0;
    pfs->banks = 1;

    const auto [pak, result] = pak_for_channel(channel);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    pfs->version = 0;
    pfs->dir_size = 16;
    pfs->inode_table = 8;
    pfs->minode_table = 16;
    pfs->dir_table = 24;
    pfs->inode_start_page = 5;
    pfs->status = pfs_initialized;
    return_error(context, twine::pfs::Ok);
}

extern "C" void osPfsInit_recomp(uint8_t* rdram, recomp_context* context) {
    osPfsInitPak_recomp(rdram, context);
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    if (!valid_rdram_range(context->r5, sizeof(uint32_t))) {
        return_error(context, twine::pfs::Invalid);
        return;
    }
    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    int32_t bytes = 0;
    const int operation = pak->free_bytes(bytes);
    if (operation == twine::pfs::Ok) {
        write_u32(rdram, context->r5, static_cast<uint32_t>(bytes));
    }
    return_error(context, operation);
}

extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    if (!valid_rdram_range(context->r5, sizeof(uint32_t)) ||
        !valid_rdram_range(context->r6, sizeof(uint32_t))) {
        return_error(context, twine::pfs::Invalid);
        return;
    }
    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    int32_t maximum = 0;
    int32_t used = 0;
    const int operation = pak->num_files(maximum, used);
    if (operation == twine::pfs::Ok) {
        write_u32(rdram, context->r5, static_cast<uint32_t>(maximum));
        write_u32(rdram, context->r6, static_cast<uint32_t>(used));
    }
    return_error(context, operation);
}

extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    twine::PakGameName game_name{};
    twine::PakExtension extension{};
    const gpr extension_address = stack_argument(rdram, context, 0);
    const int32_t byte_count = static_cast<int32_t>(stack_argument(rdram, context, 1));
    const gpr file_number_address = stack_argument(rdram, context, 2);
    if (!read_array(rdram, context->r7, game_name) ||
        !read_array(rdram, extension_address, extension) ||
        !valid_rdram_range(file_number_address, sizeof(uint32_t))) {
        return_error(context, twine::pfs::Invalid);
        return;
    }

    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    int32_t file_number = -1;
    const int operation = pak->allocate(
        static_cast<uint16_t>(context->r5),
        static_cast<uint32_t>(context->r6),
        game_name,
        extension,
        byte_count,
        file_number);
    write_u32(rdram, file_number_address, static_cast<uint32_t>(file_number));
    return_error(context, operation);
}

extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    twine::PakGameName game_name{};
    twine::PakExtension extension{};
    const gpr extension_address = stack_argument(rdram, context, 0);
    const twine::PakGameName* game_name_pointer = nullptr;
    const twine::PakExtension* extension_pointer = nullptr;
    if (context->r7 != 0) {
        if (!read_array(rdram, context->r7, game_name)) {
            return_error(context, twine::pfs::Invalid);
            return;
        }
        game_name_pointer = &game_name;
    }
    if (extension_address != 0) {
        if (!read_array(rdram, extension_address, extension)) {
            return_error(context, twine::pfs::Invalid);
            return;
        }
        extension_pointer = &extension;
    }
    if (context->r7 == 0 && extension_address == 0 &&
        context->r5 == 0 && context->r6 == 0) {
        return_error(context, twine::pfs::Invalid);
        return;
    }

    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }
    return_error(context, pak->erase(
        static_cast<uint16_t>(context->r5),
        static_cast<uint32_t>(context->r6),
        game_name_pointer,
        extension_pointer));
}

extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    twine::PakGameName game_name{};
    twine::PakExtension extension{};
    const gpr extension_address = stack_argument(rdram, context, 0);
    const gpr file_number_address = stack_argument(rdram, context, 1);
    const twine::PakGameName* game_name_pointer = nullptr;
    const twine::PakExtension* extension_pointer = nullptr;
    if (context->r7 != 0) {
        if (!read_array(rdram, context->r7, game_name)) {
            return_error(context, twine::pfs::Invalid);
            return;
        }
        game_name_pointer = &game_name;
    }
    if (extension_address != 0) {
        if (!read_array(rdram, extension_address, extension)) {
            return_error(context, twine::pfs::Invalid);
            return;
        }
        extension_pointer = &extension;
    }
    if (!valid_rdram_range(file_number_address, sizeof(uint32_t))) {
        return_error(context, twine::pfs::Invalid);
        return;
    }

    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    int32_t file_number = -1;
    const int operation = pak->find(
        static_cast<uint16_t>(context->r5),
        static_cast<uint32_t>(context->r6),
        game_name_pointer,
        extension_pointer,
        file_number);
    write_u32(rdram, file_number_address, static_cast<uint32_t>(file_number));
    return_error(context, operation);
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    const int32_t offset = static_cast<int32_t>(context->r7);
    const int32_t byte_count = static_cast<int32_t>(stack_argument(rdram, context, 0));
    const gpr buffer_address = stack_argument(rdram, context, 1);
    const uint8_t flag = static_cast<uint8_t>(context->r6);
    if ((flag != 0 && flag != 1) || byte_count <= 0 ||
        byte_count > static_cast<int32_t>(0x8000) ||
        !valid_rdram_range(buffer_address, static_cast<size_t>(byte_count))) {
        return_error(context, twine::pfs::Invalid);
        return;
    }

    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    std::vector<uint8_t> data(static_cast<size_t>(byte_count));
    if (flag == 1) {
        for (size_t index = 0; index < data.size(); ++index) {
            data[index] = read_byte(rdram, buffer_address, index);
        }
    }

    const int operation = pak->read_write(
        static_cast<int32_t>(context->r5), flag == 1, offset, data);
    if (operation == twine::pfs::Ok && flag == 0) {
        for (size_t index = 0; index < data.size(); ++index) {
            write_byte(rdram, buffer_address, index, data[index]);
        }
    }
    return_error(context, operation);
}

extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    constexpr size_t state_size = 32;
    if (!valid_rdram_range(context->r6, state_size)) {
        return_error(context, twine::pfs::Invalid);
        return;
    }
    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }

    twine::PakFileState state{};
    const int operation = pak->file_state(static_cast<int32_t>(context->r5), state);
    if (operation == twine::pfs::Ok) {
        write_u32(rdram, context->r6 + 0, state.file_size);
        write_u32(rdram, context->r6 + 4, state.game_code);
        write_u16(rdram, context->r6 + 8, state.company_code);
        for (size_t index = 0; index < state.extension.size(); ++index) {
            write_byte(rdram, context->r6, 10 + index, state.extension[index]);
        }
        for (size_t index = 0; index < state.game_name.size(); ++index) {
            write_byte(rdram, context->r6, 14 + index, state.game_name[index]);
        }
    }
    return_error(context, operation);
}

extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    OSPfs* pfs = get_pfs(rdram, context->r4);
    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    if (pak == nullptr) {
        return_error(context, result);
        return;
    }
    const int operation = pak->check_and_repair();
    if (operation == twine::pfs::Ok && pfs != nullptr) {
        pfs->status &= ~2;
    }
    return_error(context, operation);
}

extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* context) {
    std::lock_guard lock(pak_mutex);
    const auto [pak, result] = pak_for_pfs(rdram, context->r4);
    return_error(context, pak == nullptr ? result : pak->repair_id());
}

twine::state::Bytes twine::state::capture_storage(uint8_t*) {
    Writer out; out.u32(1); out.blob(capture_profile_session());
    std::lock_guard lock(pak_mutex);
    for (const auto& pak : paks) {
        out.scalar(bool(pak));
        if (pak) { const auto saved = pak->capture_session(); out.scalar(saved.loaded); out.blob(saved.bytes); }
    }
    return std::move(out.bytes);
}
std::unique_ptr<twine::state::PreparedOwner> twine::state::prepare_storage(std::span<const uint8_t> bytes) {
    Reader in(bytes); if (in.u32() != 1) throw std::runtime_error("Invalid storage state schema");
    auto profile = prepare_profile_session(in.blob(4096));
    std::array<std::unique_ptr<ControllerPak>, 4> saved;
    std::lock_guard lock(pak_mutex);
    for (size_t i = 0; i < saved.size(); ++i) {
        if (!in.scalar<bool>()) continue;
        ControllerPak::SessionState image;
        image.loaded = in.scalar<bool>();
        const auto contents = in.blob(image.bytes.size());
        if (contents.size() != image.bytes.size()) throw std::runtime_error("Invalid controller-pak state size");
        std::copy(contents.begin(), contents.end(), image.bytes.begin());
        saved[i] = std::make_unique<ControllerPak>(pak_directory / ("controller-" + std::to_string(i + 1) + ".mpk"), uint32_t(i + 1));
        saved[i]->prepare_session(image);
    }
    in.end();
    return prepared_owner([profile = std::move(profile), saved = std::move(saved)]() mutable noexcept {
        profile->commit();
        std::lock_guard lock(pak_mutex); paks.swap(saved);
    });
}
