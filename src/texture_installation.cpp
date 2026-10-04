#include "texture_installation.hpp"
#include "atomic_file.hpp"
#include <array>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace twine::textures {
std::filesystem::path pack_path(const std::filesystem::path& folder) {
    return folder / "cache" / "textures" / "coordinate-v5.rtz";
}

std::filesystem::path pack_receipt_path(const std::filesystem::path& folder) {
    return folder / "cache" / "textures" / "coordinate-v5.installed";
}

void validate_pack_location(const std::filesystem::path& folder) {
    for (const auto& path : {folder / "cache", folder / "cache" / "textures",
                            pack_path(folder), pack_receipt_path(folder)}) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::filesystem::filesystem_error("Cannot inspect texture storage", path, error);
        if (std::filesystem::is_symlink(status) ||
            (std::filesystem::exists(status) &&
             !(path == pack_path(folder) || path == pack_receipt_path(folder)
                 ? std::filesystem::is_regular_file(status) : std::filesystem::is_directory(status))))
            throw std::runtime_error("Enhanced texture storage must contain regular files and directories, not links");
    }
}

std::string pack_file_identity(const std::filesystem::path& path) {
    std::string identity = "TWINE textures v5 receipt 1\n" + std::to_string(pack_size) + "\n" +
        std::to_string(pack_hash) + "\n";
    const auto append = [&](auto value) { identity += std::to_string(value) + "\n"; };
#ifdef _WIN32
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(file, &info) ||
        !GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        ((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != pack_size) return {};
    identity += "windows\n";
    append(info.dwVolumeSerialNumber);
    append((uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
    append(basic.LastWriteTime.QuadPart);
    append(basic.ChangeTime.QuadPart);
#else
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || uint64_t(info.st_size) != pack_size) return {};
    identity += "posix\n";
    append(info.st_dev);
    append(info.st_ino);
    append(info.st_mtim.tv_sec);
    append(info.st_mtim.tv_nsec);
    append(info.st_ctim.tv_sec);
    append(info.st_ctim.tv_nsec);
#endif
    return identity;
}

std::filesystem::path installed_pack(const std::filesystem::path& folder) {
    try {
        validate_pack_location(folder);
        const auto path = pack_path(folder);
        const auto identity = pack_file_identity(path);
        if (identity.empty()) return {};
        std::ifstream input(pack_receipt_path(folder), std::ios::binary);
        std::array<char, 512> bytes{};
        input.read(bytes.data(), bytes.size());
        if (input.eof() && !input.bad() &&
            std::string_view(bytes.data(), size_t(input.gcount())) == identity) return path;
    } catch (const std::exception&) {
        return {};
    }
    return {};
}

void record_pack_installation(const std::filesystem::path& folder, std::string_view identity) {
    validate_pack_location(folder);
    if (identity.empty() || identity != pack_file_identity(pack_path(folder)))
        throw std::runtime_error("Enhanced texture archive changed during verification; retry with an unchanged file");
    const auto result = files::write_atomic(pack_receipt_path(folder),
        {reinterpret_cast<const uint8_t*>(identity.data()), identity.size()});

}
}
