#include "texture_pack.hpp"
#include "texture_installation.hpp"
#include "texture_bank.hpp"
#include "texture_native.hpp"
#include "atomic_file.hpp"
#include <array>
#include <deque>
#include <future>
#include <map>
#include <fstream>
#include <ctime>
#include <stdexcept>
#include <algorithm>
#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"
#include "miniz/miniz.h"
namespace twine::textures {
using Bytes = std::vector<uint8_t>;
namespace {
constexpr uint64_t expected_size = pack_size, expected_hash = pack_hash;
void validate_cache_path(const std::filesystem::path &folder) {
    validate_pack_location(folder);
    for (const auto &path :
         {folder / "cache", folder / "cache" / "textures", folder / "cache" / "textures" / "coordinate-v5.rtz",
          folder / "cache" / "textures" / "coordinate-v2.rtz",
          folder / "cache" / "textures" / "coordinate-v3.rtz",
          folder / "cache" / "textures" / "coordinate-v4.rtz",
          folder / "cache" / "textures" / "coordinate-v6.rtz"}) {
        if (std::filesystem::is_symlink(path))
            throw std::runtime_error("Enhanced texture storage must not be a symbolic link");
    }
}
std::pair<uint64_t, uint64_t> digest_file(const std::filesystem::path &path, std::stop_token stop,
                                       const VerificationProgress& progress) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() < 0 || uint64_t(in.tellg()) > 8ULL * 1024 * 1024 * 1024)
        return {};
    const uint64_t size = uint64_t(in.tellg());
    in.seekg(0);
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    std::array<char, 65536> buffer;
    uint64_t read = 0, reported_percent = 0;
    if (progress) progress(0, size);
    while (in.read(buffer.data(), buffer.size()) || in.gcount()) {
        if (stop.stop_requested())
            throw PreparationCancelled{};
        XXH3_64bits_update(&state, buffer.data(), size_t(in.gcount()));
        read += uint64_t(in.gcount());
        const uint64_t percent = size ? read * 100 / size : 100;
        if (progress && percent != reported_percent) {
            progress(read, size);
            reported_percent = percent;
        }
    }
    return in.eof() ? std::pair{size, XXH3_64bits_digest(&state)} : std::pair<uint64_t, uint64_t>{};
}
}

bool has_pack_cache(const std::filesystem::path& folder) {
    for (const auto* name : {"coordinate-v5.rtz", "coordinate-v2.rtz", "coordinate-v3.rtz",
                             "coordinate-v4.rtz", "coordinate-v6.rtz"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(folder / "cache" / "textures" / name, error)) return true;
    }
    return false;
}

bool has_prepared_pack(const std::filesystem::path& folder) {
    const auto path = folder / "cache" / "textures" / "coordinate-v5.rtz";
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) &&
           std::filesystem::file_size(path, error) == expected_size && !error;
}

std::filesystem::path find_prepared_pack(const std::filesystem::path& folder, std::stop_token stop,
                                        const VerificationProgress& progress) {
    if (stop.stop_requested()) throw PreparationCancelled{};
    validate_cache_path(folder);
    const auto path = folder / "cache" / "textures" / "coordinate-v5.rtz";
    if (!installed_pack(folder).empty()) {

        return path;
    }
    const auto identity = pack_file_identity(path);
    if (identity.empty() || digest_file(path, stop, progress) != std::pair{expected_size, expected_hash})
        return {};
    if (stop.stop_requested()) throw PreparationCancelled{};
    record_pack_installation(folder, identity);

    return path;
}

void remove_pack_cache(const std::filesystem::path &folder) {
    validate_cache_path(folder);
    std::filesystem::remove(pack_receipt_path(folder));
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v5.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v2.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v3.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v4.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v6.rtz");

    const auto legacy = folder / "cache" / "textures-coordinate-v1";
    if (std::filesystem::is_symlink(legacy))
        throw std::runtime_error("Legacy texture storage must not be a symbolic link");
    if (!std::filesystem::exists(legacy))
        return;
    for (const auto &file : std::filesystem::directory_iterator(legacy)) {
        if (file.is_symlink() || !file.is_regular_file())
            continue;
        const auto name = file.path().filename().string();
        if (name.size() != 20 || name.substr(16) != ".twt" || !std::all_of(name.begin(), name.begin() + 16, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }))
            continue;
        std::ifstream input(file.path(), std::ios::binary);
        std::array<uint8_t, 16> header{};
        constexpr std::array<uint8_t, 8> magic{'T', 'W', 'I', 'N', 'E', 'T', 'X', 1};
        if (!input.read(reinterpret_cast<char *>(header.data()), header.size()) ||
            !std::equal(magic.begin(), magic.end(), header.begin()))
            continue;
        uint64_t hash = 0;
        for (unsigned i = 0; i < 8; ++i)
            hash |= uint64_t(header[8 + i]) << (i * 8);
        char expected[21];
        std::snprintf(expected, sizeof(expected), "%016llx.twt", static_cast<unsigned long long>(hash));
        input.close();
        if (name == expected)
            std::filesystem::remove(file.path());
    }
    if (std::filesystem::is_empty(legacy))
        std::filesystem::remove(legacy);
}
void write_pack(std::span<const uint8_t> rom, FILE *file, const std::function<void(size_t)> &progress,
                std::stop_token shutdown) {
    if (rom.size() != 33554432 || XXH3_64bits(rom.data(), rom.size()) != 0x20AC5D898372B3C7ULL)
        throw std::runtime_error("Reconstruction requires the validated TWINE North America revision 0 ROM");
    if (!file)
        throw std::runtime_error("Reconstruction requires an owned seekable output");
    if (shutdown.stop_requested())
        throw PreparationCancelled{};
    size_t total = 0, unique = 0;
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_cfile(&zip, file, MZ_ZIP_FLAG_WRITE_ZIP64))
        throw std::runtime_error("Cannot create reconstruction archive");
    struct Owner {
        mz_zip_archive *zip;
        ~Owner() { mz_zip_writer_end(zip); }
    } owner{&zip};
    std::tm calendar{};
    calendar.tm_year = 80;
    calendar.tm_mday = 1;
    calendar.tm_isdst = -1;
    MZ_TIME_T stamp = std::mktime(&calendar);
    if (stamp == MZ_TIME_T(-1))
        throw std::runtime_error("Cannot represent reconstruction archive time");
    const auto add = [&](const std::string &name, std::span<const uint8_t> bytes) {
        if (!mz_zip_writer_add_mem_ex_v2(&zip, name.c_str(), bytes.data(), bytes.size(), nullptr, 0, 1, 0, 0, &stamp,
                                         nullptr, 0, nullptr, 0, 8))
            throw std::runtime_error("Cannot write reconstruction archive texture");
    };
    struct Pending {
        std::string name;
        std::future<Bytes> bytes;
    };
    std::deque<Pending> pending;
    std::map<uint64_t, uint64_t> identities;
    std::string metadata =
        R"({"configuration":{"autoPath":"rt64","configurationVersion":3,"hashVersion":6,"defaultOperation":"stream","defaultShift":"half"},"textures":[)";
    const auto finish_one = [&] {
        if (shutdown.stop_requested())
            throw PreparationCancelled{};
        const auto bytes = pending.front().bytes.get();
        add(pending.front().name, bytes);
        pending.pop_front();
    };

    visit_bank_tiles(
        rom,
        [&](NativeTile &&tile) {
            if (shutdown.stop_requested())
                throw PreparationCancelled{};
            ++total;

            auto image = decode_native(tile);
            const auto content = XXH3_64bits(image.rgba.data(), image.rgba.size());
            const auto [it, inserted] = identities.emplace(tile.hash, content);
            if (!inserted) {
                if (it->second != content)
                    throw std::runtime_error("Conflicting original texture identity");
                return;
            }
            char hash[17];
            std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(tile.hash));
            if (unique++)
                metadata += ',';
            metadata += std::string("{\"hashes\":{\"rt64\":\"") + hash + "\"},\"path\":\"" + hash +
                        "\",\"preserveOriginalAlpha\":true";
            metadata += '}';
            pending.push_back({std::string(hash) + ".dds", std::async(std::launch::async, [image = std::move(image)] {
                                   return make_dds(reconstruct_material(image));
                               })});
            if (pending.size() == 4)
                finish_one();
            if (unique % 64 == 0) {

                if (progress)
                    progress(unique);
            }
        },
        shutdown);
    while (!pending.empty())
        finish_one();
    if (unique != prepared_texture_count)
        throw std::runtime_error("Prepared texture archive has incomplete native coverage");
    metadata += "]}";
    add("rt64.json", {reinterpret_cast<const uint8_t *>(metadata.data()), metadata.size()});
    if (!mz_zip_writer_finalize_archive(&zip))
        throw std::runtime_error("Cannot finalize reconstruction archive");
    if (progress)
        progress(unique);
}

std::filesystem::path prepare_pack(std::span<const uint8_t> rom, const std::filesystem::path &folder,
                                   const std::function<void(size_t)> &progress, std::stop_token shutdown,
                                   const VerificationProgress& verification) {
    if (rom.size() != 33554432 || XXH3_64bits(rom.data(), rom.size()) != 0x20AC5D898372B3C7ULL)
        throw std::runtime_error("Reconstruction requires the validated TWINE North America revision 0 ROM");
    validate_cache_path(folder);
    const auto path = folder / "cache" / "textures" / "coordinate-v5.rtz";
    if (shutdown.stop_requested())
        throw PreparationCancelled{};

    if (!find_prepared_pack(folder, shutdown, verification).empty()) {

        std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v2.rtz");
        std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v3.rtz");
        std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v4.rtz");
        std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v6.rtz");
        return path;
    }

    if (progress)
        progress(0);
    uint64_t published_size = 0, published_hash = 0;
    const auto result = files::write_atomic_stream(path, 8ULL * 1024 * 1024 * 1024, [&](FILE *file) {
        write_pack(rom, file, progress, shutdown);
        if (std::fflush(file) != 0 || std::fseek(file, 0, SEEK_SET) != 0)
            throw std::runtime_error("Cannot verify reconstruction archive before publication");
        XXH3_state_t state;
        XXH3_64bits_reset(&state);
        std::array<uint8_t, 65536> bytes;
        while (const size_t count = std::fread(bytes.data(), 1, bytes.size(), file)) {
            if (shutdown.stop_requested())
                throw PreparationCancelled{};
            published_size += count;
            XXH3_64bits_update(&state, bytes.data(), count);
        }
        if (std::ferror(file))
            throw std::runtime_error("Cannot read completed reconstruction archive");
        published_hash = XXH3_64bits_digest(&state);
        if (published_size != expected_size || published_hash != expected_hash)
            throw std::runtime_error("Generated reconstruction archive does not match the verified release identity");
    });

    record_pack_installation(folder, pack_file_identity(path));

    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v2.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v3.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v4.rtz");
    std::filesystem::remove(folder / "cache" / "textures" / "coordinate-v6.rtz");
    return path;
}
}
