#include <stdexcept>
#include "controller_pak.hpp"

#include <algorithm>
#include <fstream>

#include "librecomp/files.hpp"

namespace {

constexpr std::array<size_t, 4> id_block_offsets = {32, 96, 128, 192};
constexpr size_t inode_offset = 256;
constexpr size_t inode_backup_offset = 512;
constexpr size_t directory_offset = 768;
constexpr size_t directory_entry_size = 32;
constexpr uint16_t inode_terminal = 1;
constexpr uint16_t inode_free = 3;
constexpr uint8_t directory_occupied = 2;

uint16_t read_be16(const uint8_t* data) {
    return static_cast<uint16_t>((uint16_t{data[0]} << 8) | data[1]);
}

uint32_t read_be32(const uint8_t* data) {
    return (uint32_t{data[0]} << 24) |
           (uint32_t{data[1]} << 16) |
           (uint32_t{data[2]} << 8) |
           uint32_t{data[3]};
}

void write_be16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

void write_be32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value >> 24);
    data[1] = static_cast<uint8_t>(value >> 16);
    data[2] = static_cast<uint8_t>(value >> 8);
    data[3] = static_cast<uint8_t>(value);
}

}

twine::ControllerPak::ControllerPak(std::filesystem::path path, uint32_t serial_suffix)
    : path_(std::move(path)), serial_suffix_(serial_suffix) {
}

int twine::ControllerPak::load_or_create() {
    if (loaded_) {
        return pfs::Ok;
    }

    std::error_code error;
    const bool primary_exists = std::filesystem::exists(path_, error);
    std::filesystem::path backup_path = path_;
    backup_path += ".bak";
    const bool backup_exists = std::filesystem::exists(backup_path, error);

    if (read_exact(path_) || read_exact(backup_path)) {
        loaded_ = true;
        int result = repair_id();
        if (result == pfs::Ok) {
            result = check_and_repair();
        }
        if (result != pfs::Ok) {
            loaded_ = false;
        }
        return result;
    }

    if (primary_exists || backup_exists) {
        return pfs::IdFatal;
    }

    format();
    loaded_ = true;
    const int result = persist();
    if (result != pfs::Ok) {
        loaded_ = false;
    }
    return result;
}

int twine::ControllerPak::load_existing_read_only() {
    if (loaded_) {
        return pfs::Ok;
    }
    std::filesystem::path backup_path = path_;
    backup_path += ".bak";
    for (const auto& candidate : {path_, backup_path}) {
        if (!read_exact(candidate)) {
            continue;
        }
        const bool primary_valid = valid_inode_page(inode_offset);
        const bool backup_valid = valid_inode_page(inode_backup_offset);
        if (!primary_valid && !backup_valid) {
            continue;
        }
        if (!primary_valid) {
            std::copy_n(
                bytes_.begin() + inode_backup_offset,
                PageSize,
                bytes_.begin() + inode_offset);
        }
        loaded_ = true;
        return pfs::Ok;
    }
    return pfs::IdFatal;
}

bool twine::ControllerPak::read_exact(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.read(reinterpret_cast<char*>(bytes_.data()), bytes_.size());
    return input.gcount() == static_cast<std::streamsize>(bytes_.size()) &&
           input.peek() == std::char_traits<char>::eof();
}

void twine::ControllerPak::format() {
    bytes_.fill(0);

    std::array<uint8_t, 32> id{};
    write_id_block(id);
    for (const size_t offset : id_block_offsets) {
        std::copy(id.begin(), id.end(), bytes_.begin() + offset);
    }

    for (size_t page = FirstDataPage; page < PageCount; ++page) {
        set_inode(page, inode_free);
    }
    update_inode_checksum();
}

void twine::ControllerPak::write_id_block(std::span<uint8_t, 32> output) const {
    std::fill(output.begin(), output.end(), uint8_t{0});
    constexpr std::array<uint32_t, 6> serial = {
        0x5457494EU, 0x452D4E36U, 0x34524543U, 0x4F4D502DU, 0x50414B00U, 0x00000000U,
    };
    for (size_t index = 0; index < serial.size(); ++index) {
        uint32_t value = serial[index];
        if (index == serial.size() - 1) {
            value |= serial_suffix_;
        }
        write_be32(output.data() + index * sizeof(uint32_t), value);
    }
    write_be16(output.data() + 24, 1);
    output[26] = 1;
    output[27] = 0;

    uint16_t sum = 0;
    for (size_t offset = 0; offset < 28; offset += 2) {
        sum = static_cast<uint16_t>(sum + read_be16(output.data() + offset));
    }
    write_be16(output.data() + 28, sum);
    write_be16(output.data() + 30, static_cast<uint16_t>(0xFFF2U - sum));
}

bool twine::ControllerPak::valid_id_block(size_t offset) const {
    if (offset + 32 > bytes_.size()) {
        return false;
    }

    const uint8_t* id = bytes_.data() + offset;
    uint16_t sum = 0;
    for (size_t index = 0; index < 28; index += 2) {
        sum = static_cast<uint16_t>(sum + read_be16(id + index));
    }
    return read_be16(id + 24) == 1 &&
           id[26] == 1 &&
           read_be16(id + 28) == sum &&
           read_be16(id + 30) == static_cast<uint16_t>(0xFFF2U - sum);
}

int twine::ControllerPak::repair_id() {
    if (!loaded_) {
        return pfs::NoPak;
    }

    const auto previous = bytes_;
    const auto valid = std::find_if(id_block_offsets.begin(), id_block_offsets.end(),
        [this](size_t offset) { return valid_id_block(offset); });

    std::array<uint8_t, 32> id{};
    if (valid != id_block_offsets.end()) {
        std::copy_n(bytes_.begin() + *valid, id.size(), id.begin());
    }
    else {
        write_id_block(id);
    }

    for (const size_t offset : id_block_offsets) {
        std::copy(id.begin(), id.end(), bytes_.begin() + offset);
    }
    return bytes_ == previous ? pfs::Ok : commit_or_restore(previous);
}

bool twine::ControllerPak::valid_inode_page(size_t offset) const {
    if (offset + PageSize > bytes_.size()) {
        return false;
    }
    uint8_t sum = 0;
    for (size_t index = FirstDataPage * 2; index < PageSize; ++index) {
        sum = static_cast<uint8_t>(sum + bytes_[offset + index]);
    }
    return bytes_[offset] == 0 && bytes_[offset + 1] == sum;
}

void twine::ControllerPak::update_inode_checksum() {
    bytes_[inode_offset] = 0;
    uint8_t sum = 0;
    for (size_t index = FirstDataPage * 2; index < PageSize; ++index) {
        sum = static_cast<uint8_t>(sum + bytes_[inode_offset + index]);
    }
    bytes_[inode_offset + 1] = sum;
    std::copy_n(bytes_.begin() + inode_offset, PageSize, bytes_.begin() + inode_backup_offset);
}

uint16_t twine::ControllerPak::inode(size_t page) const {
    return read_be16(bytes_.data() + inode_offset + page * 2);
}

void twine::ControllerPak::set_inode(size_t page, uint16_t value) {
    write_be16(bytes_.data() + inode_offset + page * 2, value);
}

size_t twine::ControllerPak::directory_offset(size_t file_number) const {
    return ::directory_offset + file_number * directory_entry_size;
}

bool twine::ControllerPak::directory_used(size_t file_number) const {
    const uint8_t* entry = bytes_.data() + directory_offset(file_number);
    return read_be32(entry) != 0 && read_be16(entry + 4) != 0;
}

void twine::ControllerPak::clear_directory(size_t file_number) {
    const size_t offset = directory_offset(file_number);
    std::fill_n(bytes_.begin() + offset, directory_entry_size, uint8_t{0});
}

int twine::ControllerPak::first_empty_directory() const {
    for (size_t file = 0; file < DirectoryEntries; ++file) {
        const uint8_t* entry = bytes_.data() + directory_offset(file);
        if (read_be32(entry) == 0 && read_be16(entry + 4) == 0) {
            return static_cast<int>(file);
        }
    }
    return -1;
}

int twine::ControllerPak::chain_for_file(
    int32_t file_number,
    std::array<uint8_t, PageCount>& pages,
    size_t& count
) const {
    count = 0;
    if (!loaded_ || file_number < 0 || file_number >= static_cast<int32_t>(DirectoryEntries) ||
        !directory_used(static_cast<size_t>(file_number))) {
        return pfs::Invalid;
    }

    const uint8_t* entry = bytes_.data() + directory_offset(static_cast<size_t>(file_number));
    if (entry[6] != 0 || entry[7] < FirstDataPage || entry[7] >= PageCount) {
        return pfs::Inconsistent;
    }

    std::array<bool, PageCount> seen{};
    uint8_t page = entry[7];
    while (true) {
        if (page < FirstDataPage || page >= PageCount || seen[page] || count >= pages.size()) {
            return pfs::Inconsistent;
        }
        seen[page] = true;
        pages[count++] = page;

        const uint16_t next = inode(page);
        if (next == inode_terminal) {
            return pfs::Ok;
        }
        if ((next >> 8) != 0 || (next & 0xFFU) < FirstDataPage || (next & 0xFFU) >= PageCount) {
            return pfs::Inconsistent;
        }
        page = static_cast<uint8_t>(next);
    }
}

int twine::ControllerPak::check_and_repair() {
    if (!loaded_) {
        return pfs::NoPak;
    }

    const bool primary_valid = valid_inode_page(inode_offset);
    const bool backup_valid = valid_inode_page(inode_backup_offset);
    if (!primary_valid && !backup_valid) {
        return pfs::Inconsistent;
    }

    const auto previous = bytes_;
    if (!primary_valid) {
        std::copy_n(bytes_.begin() + inode_backup_offset, PageSize, bytes_.begin() + inode_offset);
    }

    std::array<uint16_t, PageCount> rebuilt{};
    rebuilt.fill(0);
    for (size_t page = FirstDataPage; page < PageCount; ++page) {
        rebuilt[page] = inode_free;
    }

    std::array<bool, PageCount> globally_used{};
    for (size_t file = 0; file < DirectoryEntries; ++file) {
        uint8_t* entry = bytes_.data() + directory_offset(file);
        const uint32_t game_code = read_be32(entry);
        const uint16_t company_code = read_be16(entry + 4);
        if (game_code == 0 && company_code == 0) {
            if (std::any_of(entry, entry + directory_entry_size, [](uint8_t value) { return value != 0; })) {
                clear_directory(file);
            }
            continue;
        }

        bool valid = game_code != 0 && company_code != 0 &&
                     entry[6] == 0 && entry[7] >= FirstDataPage && entry[7] < PageCount;
        std::array<uint8_t, PageCount> chain{};
        std::array<bool, PageCount> locally_used{};
        size_t count = 0;
        uint8_t page = entry[7];

        while (valid) {
            if (page < FirstDataPage || page >= PageCount ||
                locally_used[page] || globally_used[page] || count >= chain.size()) {
                valid = false;
                break;
            }
            locally_used[page] = true;
            chain[count++] = page;

            const uint16_t next = inode(page);
            if (next == inode_terminal) {
                break;
            }
            if ((next >> 8) != 0 || (next & 0xFFU) < FirstDataPage || (next & 0xFFU) >= PageCount) {
                valid = false;
                break;
            }
            page = static_cast<uint8_t>(next);
        }

        if (!valid || count == 0 || inode(chain[count - 1]) != inode_terminal) {
            clear_directory(file);
            continue;
        }

        for (size_t index = 0; index < count; ++index) {
            globally_used[chain[index]] = true;
            rebuilt[chain[index]] = index + 1 == count ? inode_terminal : chain[index + 1];
        }
    }

    for (size_t page = 0; page < PageCount; ++page) {
        set_inode(page, rebuilt[page]);
    }
    update_inode_checksum();
    return bytes_ == previous ? pfs::Ok : commit_or_restore(previous);
}

int twine::ControllerPak::free_bytes(int32_t& bytes) const {
    if (!loaded_) {
        return pfs::NoPak;
    }
    int32_t free_pages = 0;
    for (size_t page = FirstDataPage; page < PageCount; ++page) {
        free_pages += inode(page) == inode_free;
    }
    bytes = free_pages * static_cast<int32_t>(PageSize);
    return pfs::Ok;
}

int twine::ControllerPak::num_files(int32_t& maximum, int32_t& used) const {
    if (!loaded_) {
        return pfs::NoPak;
    }
    used = 0;
    for (size_t file = 0; file < DirectoryEntries; ++file) {
        used += directory_used(file);
    }
    maximum = static_cast<int32_t>(DirectoryEntries);
    return pfs::Ok;
}

int twine::ControllerPak::find(
    uint16_t company_code,
    uint32_t game_code,
    const PakGameName* game_name,
    const PakExtension* extension,
    int32_t& file_number
) const {
    file_number = -1;
    if (!loaded_) {
        return pfs::NoPak;
    }

    for (size_t file = 0; file < DirectoryEntries; ++file) {
        const uint8_t* entry = bytes_.data() + directory_offset(file);
        if (read_be32(entry) == game_code &&
            read_be16(entry + 4) == company_code &&
            (extension == nullptr ||
             std::equal(extension->begin(), extension->end(), entry + 12)) &&
            (game_name == nullptr ||
             std::equal(game_name->begin(), game_name->end(), entry + 16))) {
            file_number = static_cast<int32_t>(file);
            return pfs::Ok;
        }
    }
    return pfs::Invalid;
}

int twine::ControllerPak::allocate(
    uint16_t company_code,
    uint32_t game_code,
    const PakGameName& game_name,
    const PakExtension& extension,
    int32_t byte_count,
    int32_t& file_number
) {
    file_number = -1;
    if (!loaded_) {
        return pfs::NoPak;
    }
    if (company_code == 0 || game_code == 0 || byte_count <= 0) {
        return pfs::Invalid;
    }

    int32_t existing = -1;
    if (find(company_code, game_code, &game_name, &extension, existing) == pfs::Ok) {
        file_number = existing;
        return pfs::Exists;
    }

    const uint64_t page_count =
        (static_cast<uint64_t>(byte_count) + PageSize - 1) / PageSize;
    if (page_count > PageCount - FirstDataPage) {
        return pfs::DataFull;
    }

    std::array<uint8_t, PageCount> free_pages{};
    size_t free_count = 0;
    for (size_t page = FirstDataPage; page < PageCount && free_count < page_count; ++page) {
        if (inode(page) == inode_free) {
            free_pages[free_count++] = static_cast<uint8_t>(page);
        }
    }
    if (free_count != page_count) {
        return pfs::DataFull;
    }

    const int directory = first_empty_directory();
    if (directory < 0) {
        return pfs::DirectoryFull;
    }

    const auto previous = bytes_;
    for (size_t index = 0; index < free_count; ++index) {
        set_inode(free_pages[index],
            index + 1 == free_count ? inode_terminal : free_pages[index + 1]);
        const size_t offset = static_cast<size_t>(free_pages[index]) * PageSize;
        std::fill_n(bytes_.begin() + offset, PageSize, uint8_t{0});
    }

    uint8_t* entry = bytes_.data() + directory_offset(static_cast<size_t>(directory));
    std::fill_n(entry, directory_entry_size, uint8_t{0});
    write_be32(entry, game_code);
    write_be16(entry + 4, company_code);
    entry[6] = 0;
    entry[7] = free_pages[0];
    std::copy(extension.begin(), extension.end(), entry + 12);
    std::copy(game_name.begin(), game_name.end(), entry + 16);
    update_inode_checksum();

    const int result = commit_or_restore(previous);
    if (result == pfs::Ok) {
        file_number = directory;
    }
    return result;
}

int twine::ControllerPak::erase(
    uint16_t company_code,
    uint32_t game_code,
    const PakGameName* game_name,
    const PakExtension* extension
) {
    if (company_code == 0 || game_code == 0) {
        return pfs::Invalid;
    }

    int32_t file_number = -1;
    const int find_result = find(company_code, game_code, game_name, extension, file_number);
    if (find_result != pfs::Ok) {
        return find_result;
    }

    std::array<uint8_t, PageCount> pages{};
    size_t count = 0;
    const int chain_result = chain_for_file(file_number, pages, count);
    if (chain_result != pfs::Ok) {
        return chain_result;
    }

    const auto previous = bytes_;
    for (size_t index = 0; index < count; ++index) {
        set_inode(pages[index], inode_free);
        const size_t offset = static_cast<size_t>(pages[index]) * PageSize;
        std::fill_n(bytes_.begin() + offset, PageSize, uint8_t{0});
    }
    clear_directory(static_cast<size_t>(file_number));
    update_inode_checksum();
    return commit_or_restore(previous);
}

int twine::ControllerPak::read_write(
    int32_t file_number,
    bool write,
    int32_t offset,
    std::span<uint8_t> data
) {
    if (offset < 0 || data.empty() || (offset % 32) != 0 || (data.size() % 32) != 0) {
        return pfs::Invalid;
    }

    std::array<uint8_t, PageCount> pages{};
    size_t count = 0;
    const int chain_result = chain_for_file(file_number, pages, count);
    if (chain_result != pfs::Ok) {
        return chain_result;
    }

    const size_t file_size = count * PageSize;
    const size_t start = static_cast<size_t>(offset);
    if (start > file_size || data.size() > file_size - start) {
        return pfs::Invalid;
    }

    uint8_t* entry = bytes_.data() + directory_offset(static_cast<size_t>(file_number));
    if (!write && (entry[8] & directory_occupied) == 0) {
        return pfs::BadData;
    }

    std::array<uint8_t, PakSize> previous;
    if (write) {
        previous = bytes_;
    }
    size_t copied = 0;
    while (copied < data.size()) {
        const size_t file_position = start + copied;
        const size_t page_index = file_position / PageSize;
        const size_t page_offset = file_position % PageSize;
        const size_t chunk = std::min(data.size() - copied, PageSize - page_offset);
        uint8_t* pak_data = bytes_.data() + static_cast<size_t>(pages[page_index]) * PageSize + page_offset;
        if (write) {
            std::copy_n(data.begin() + copied, chunk, pak_data);
        }
        else {
            std::copy_n(pak_data, chunk, data.begin() + copied);
        }
        copied += chunk;
    }

    if (!write) {
        return pfs::Ok;
    }
    entry[8] |= directory_occupied;
    return commit_or_restore(previous);
}

int twine::ControllerPak::file_state(int32_t file_number, PakFileState& state) const {
    std::array<uint8_t, PageCount> pages{};
    size_t count = 0;
    const int result = chain_for_file(file_number, pages, count);
    if (result != pfs::Ok) {
        return result;
    }

    const uint8_t* entry = bytes_.data() + directory_offset(static_cast<size_t>(file_number));
    state.file_size = static_cast<uint32_t>(count * PageSize);
    state.game_code = read_be32(entry);
    state.company_code = read_be16(entry + 4);
    std::copy_n(entry + 12, state.extension.size(), state.extension.begin());
    std::copy_n(entry + 16, state.game_name.size(), state.game_name.begin());
    return pfs::Ok;
}

int twine::ControllerPak::persist() {
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) {
        return pfs::ControllerFailure;
    }

    std::ofstream output = recomp::open_output_file_with_backup(
        path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return pfs::ControllerFailure;
    }
    output.write(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
    return recomp::finalize_output_file_with_backup(path_, output)
        ? pfs::Ok
        : pfs::ControllerFailure;
}

int twine::ControllerPak::commit_or_restore(const std::array<uint8_t, PakSize>& previous) {
    const int result = persist();
    if (result != pfs::Ok) {
        bytes_ = previous;
    }
    return result;
}

void twine::ControllerPak::prepare_session(const SessionState& saved) {
    bytes_ = saved.bytes; loaded_ = saved.loaded;
    if (!loaded_) return;
    if (!valid_id_block(0x20) || !valid_inode_page(0x100))
        throw std::runtime_error("Invalid controller-pak state metadata");
    std::array<bool, PageCount> used{};
    for (size_t file = 0; file < DirectoryEntries; ++file) {
        if (!directory_used(file)) continue;
        std::array<uint8_t, PageCount> pages{}; size_t count = 0;
        if (chain_for_file(int32_t(file), pages, count) != pfs::Ok)
            throw std::runtime_error("Invalid controller-pak state chain");
        for (size_t i = 0; i < count; ++i) {
            if (used[pages[i]]) throw std::runtime_error("Overlapping controller-pak state files");
            used[pages[i]] = true;
        }
    }
}
