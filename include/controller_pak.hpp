#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>

namespace twine {

namespace pfs {
constexpr int Ok = 0;
constexpr int NoPak = 1;
constexpr int NewPak = 2;
constexpr int Inconsistent = 3;
constexpr int ControllerFailure = 4;
constexpr int Invalid = 5;
constexpr int BadData = 6;
constexpr int DataFull = 7;
constexpr int DirectoryFull = 8;
constexpr int Exists = 9;
constexpr int IdFatal = 10;
constexpr int WrongDevice = 11;
}

using PakGameName = std::array<uint8_t, 16>;
using PakExtension = std::array<uint8_t, 4>;

struct PakFileState {
    uint32_t file_size;
    uint32_t game_code;
    uint16_t company_code;
    PakExtension extension;
    PakGameName game_name;
};

class ControllerPak {
public:
    struct SessionState {
        std::array<uint8_t, 0x8000> bytes{};
        bool loaded = false;
    };
    SessionState capture_session() const { return {bytes_, loaded_}; }

    void prepare_session(const SessionState& saved);

    explicit ControllerPak(std::filesystem::path path, uint32_t serial_suffix = 0);

    int load_or_create();
    int load_existing_read_only();
    int repair_id();
    int check_and_repair();

    int free_bytes(int32_t& bytes) const;
    int num_files(int32_t& maximum, int32_t& used) const;
    int find(
        uint16_t company_code,
        uint32_t game_code,
        const PakGameName* game_name,
        const PakExtension* extension,
        int32_t& file_number
    ) const;
    int allocate(
        uint16_t company_code,
        uint32_t game_code,
        const PakGameName& game_name,
        const PakExtension& extension,
        int32_t byte_count,
        int32_t& file_number
    );
    int erase(
        uint16_t company_code,
        uint32_t game_code,
        const PakGameName* game_name,
        const PakExtension* extension
    );
    int read_write(int32_t file_number, bool write, int32_t offset, std::span<uint8_t> data);
    int file_state(int32_t file_number, PakFileState& state) const;

private:
    static constexpr size_t PakSize = 0x8000;
    static constexpr size_t PageSize = 256;
    static constexpr size_t FirstDataPage = 5;
    static constexpr size_t PageCount = 128;
    static constexpr size_t DirectoryEntries = 16;

    std::filesystem::path path_;
    uint32_t serial_suffix_;
    std::array<uint8_t, PakSize> bytes_{};
    bool loaded_ = false;

    void format();
    bool read_exact(const std::filesystem::path& path);
    int persist();
    int commit_or_restore(const std::array<uint8_t, PakSize>& previous);

    bool valid_id_block(size_t offset) const;
    bool valid_inode_page(size_t offset) const;
    void write_id_block(std::span<uint8_t, 32> output) const;
    void update_inode_checksum();

    uint16_t inode(size_t page) const;
    void set_inode(size_t page, uint16_t value);
    size_t directory_offset(size_t file_number) const;
    bool directory_used(size_t file_number) const;
    void clear_directory(size_t file_number);
    int first_empty_directory() const;
    int chain_for_file(int32_t file_number, std::array<uint8_t, PageCount>& pages, size_t& count) const;
};

}
