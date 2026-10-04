#include "font_textures.hpp"
#include "font_reconstruction.hpp"

#include <algorithm>
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include "gbi/rt64_f3d.h"
#include "xxHash/xxh3.h"
#include "common/rt64_load_types.h"
#include "common/rt64_tmem_hasher.h"
#include "recompui/renderer.h"
#include "twine_recomp.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
std::filesystem::path directory;
bool loaded = false;
std::atomic<std::shared_ptr<const twine::fonts::Atlas>> menu_font;
constexpr size_t dds_bytes = 128 + twine::fonts::width * twine::fonts::height *
    twine::fonts::scale * twine::fonts::scale * 4;
constexpr char rom_identity[] = "72e3e7b4ff1615bc17336d3d10e18aa9342898db063ace68922a47f5e46c48b1";

std::vector<uint8_t> read_file(const std::filesystem::path& path, size_t maximum) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum) return {};
    std::vector<uint8_t> bytes(size);
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))) return {};
    return bytes;
}

uint64_t texture_hash(const twine::fonts::Atlas& atlas) {
    std::array<uint8_t, 4096> tmem{};

    for (size_t i = 0; i < tmem.size(); ++i) {
        tmem[i ^ (((i / 64) & 1) ? 4 : 0)] = atlas.packed[i];
    }
    RT64::LoadTile tile{};
    tile.fmt = G_IM_FMT_I; tile.siz = G_IM_SIZ_4b; tile.line = 8;
    return RT64::TMEMHasher::hash(tmem.data(), tile, 128, 64, 0,
        RT64::TMEMHasher::CurrentHashVersion);
}

void write_file(const std::filesystem::path& path, std::span<const uint8_t> data) {

    std::error_code ec;
    if (std::filesystem::file_size(path, ec) == data.size() && !ec) {
        std::vector<uint8_t> existing(data.size());
        std::ifstream input(path, std::ios::binary);
        if (input.read(reinterpret_cast<char*>(existing.data()), existing.size()) &&
                std::equal(existing.begin(), existing.end(), data.begin())) { return; }
    }
    static std::atomic<uint64_t> sequence{0};
    auto temporary = path;
    temporary += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        file.close();
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Unable to publish enhanced font cache");
        }
#else
        std::filesystem::rename(temporary, path);
#endif
    }
    catch (...) {
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
}

void twine::fonts::initialize(const std::filesystem::path& config_path) {
    directory = config_path / "cache" / "native-fonts-v1";
}

void twine::fonts::reset() {
    loaded = false;

}

size_t twine::fonts::prepare_cache(const std::filesystem::path& path, const Atlases& atlases) {
    std::filesystem::create_directories(path);
    auto manifest_bytes = read_file(path / "cache.json", 8192);
    auto previous = json::parse(manifest_bytes, nullptr, false);
    if (!previous.is_object() || previous.value("version", json{}) != 1 ||
            previous.value("rom", json{}) != rom_identity ||
            previous.value("hashVersion", json{}) != RT64::TMEMHasher::CurrentHashVersion ||
            !previous.value("atlases", json{}).is_array() || previous["atlases"].size() != atlases.size()) {
        previous = {{"atlases", json::array()}};
    }
    json manifest = {{"version", 1}, {"rom", rom_identity},
        {"hashVersion", RT64::TMEMHasher::CurrentHashVersion}, {"atlases", json::array()}};
    json database = {{"configuration", {{"configurationVersion", 3},
        {"hashVersion", RT64::TMEMHasher::CurrentHashVersion},
        {"defaultOperation", "preload"}, {"defaultShift", "none"}}}, {"textures", json::array()}};
    size_t rebuilt = 0;
    for (size_t i = 0; i < atlases.size(); ++i) {
        const auto hash = texture_hash(atlases[i]);
        static_assert(sizeof(Glyph) == 5);
        const auto source = XXH3_64bits_withSeed(atlases[i].glyphs.data(),
            sizeof(atlases[i].glyphs), XXH3_64bits(atlases[i].packed.data(), atlases[i].packed.size()));
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
        const auto image_path = path / (std::string(name) + ".dds");
        const auto existing = read_file(image_path, dds_bytes);
        const auto digest = XXH3_64bits(existing.data(), existing.size());
        const auto old = i < previous["atlases"].size() ? previous["atlases"][i] : json{};
        const bool valid = old.is_object() && old.value("source", json{}) == source &&
            old.value("digest", json{}) == digest && existing.size() == dds_bytes;
        uint64_t image_digest = digest;
        if (!valid) {
            const auto image = encode_dds(reconstruct(atlases[i]));
            image_digest = XXH3_64bits(image.data(), image.size());
            write_file(image_path, image);
            ++rebuilt;
        }
        manifest["atlases"].push_back({{"source", source}, {"digest", image_digest}});
        database["textures"].push_back({{"path", name}, {"hashes", {{"rt64", name}}}});
    }
    for (const auto& [filename, document] : {std::pair{"rt64.json", database}, std::pair{"cache.json", manifest}}) {
        const auto text = document.dump(2);
        write_file(path / filename, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    }
    return rebuilt;
}

std::shared_ptr<const twine::fonts::Atlas> twine::fonts::menu_atlas() {
    return menu_font.load(std::memory_order_acquire);
}

extern "C" void twine_prepare_font_textures(uint8_t* rdram, recomp_context* ctx) {
    if (loaded || directory.empty()) { return; }
    const auto bank = uint32_t(ctx->r19);
    if (bank != uint32_t(TWINE_MEM_W(0, 0x800C0A24U))) { return; }
    try {
        twine::fonts::Atlases atlases{};
        if (!twine::fonts::read_atlases(rdram, bank, atlases)) {
            throw std::runtime_error("Unsupported native font resource metadata");
        }
        menu_font.store(std::make_shared<const twine::fonts::Atlas>(atlases[2]),
            std::memory_order_release);
        twine::fonts::prepare_cache(directory, atlases);
        recompui::renderer::set_base_texture_pack(directory);
        loaded = true;

    }
    catch (const std::exception& error) {

        loaded = true;

    }
}
