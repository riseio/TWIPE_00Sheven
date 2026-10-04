#include "texture_bank.hpp"
#include "material_query.hpp"
#include "twine_recomp.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <map>
#include <set>
#include "texture_pack.hpp"
#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"
#include "gbi/rt64_f3d.h"
#include "common/rt64_tmem_hasher.h"

extern "C" void func_80014A0C(uint8_t *, recomp_context *);
extern "C" void func_80032978(uint8_t *, recomp_context *);
extern "C" void func_80041F68(uint8_t *, recomp_context *);

namespace {
thread_local const recomp_context* conversion_query = nullptr;
}
extern "C" uint32_t twine_texture_conversion_query(const recomp_context* context) {
    return conversion_query == context;
}

namespace twine::textures {
namespace {
uint32_t word(std::span<const uint8_t> bytes, size_t p) {
    if (p > bytes.size() || bytes.size() - p < 4)
        throw std::runtime_error("Truncated texture bank word");
    return uint32_t(bytes[p]) << 24 | uint32_t(bytes[p + 1]) << 16 | uint32_t(bytes[p + 2]) << 8 | bytes[p + 3];
}
uint16_t half(std::span<const uint8_t> bytes, size_t p) {
    if (p > bytes.size() || bytes.size() - p < 2)
        throw std::runtime_error("Truncated texture bank halfword");
    return uint16_t(unsigned(bytes[p]) * 256 + bytes[p + 1]);
}
std::span<const uint8_t> extent(std::span<const uint8_t> bytes, size_t p, size_t n) {
    if (p > bytes.size() || n > bytes.size() - p)
        throw std::runtime_error("Texture bank extent outside ROM");
    return bytes.subspan(p, n);
}
class Resources {
    std::vector<uint8_t> arena;

  public:
    void convert_images(std::span<const uint8_t> records, std::vector<uint8_t>& pixels) {
        constexpr uint32_t header = 0x80300000U, images = 0x80301000U, data = 0x80400000U;
        if (records.size() % 12 || records.size() > 256 * 12 || pixels.size() > 2 * 1024 * 1024)
            throw std::runtime_error("Invalid native texture conversion extent");
        auto* rdram = arena.data();
        for (size_t i = 0; i < records.size(); ++i) TWINE_MEM_B(i, images) = records[i];
        for (size_t i = 0; i < pixels.size(); ++i) TWINE_MEM_B(i, data) = pixels[i];
        for (size_t i = 0; i < records.size(); i += 12) {
            const auto record = records.subspan(i, 12);
            const uint64_t size = uint64_t(half(record, 2) + 1U) * (half(record, 4) + 1U) *
                std::max(1U, unsigned(record[6])) / ((record[0] & 1) ? 1 : 2);
            const auto offset = word(record, 8);
            if (offset > pixels.size() || size > pixels.size() - offset)
                throw std::runtime_error("Native texture conversion exceeds its bank");
            TWINE_MEM_W(i + 8, images) = data + offset;
        }
        TWINE_MEM_W(0x18, header) = images;
        TWINE_MEM_W(0x1C, header) = records.size() / 12;
        recomp_context context{};
        context.r29 = int32_t(0x807FF000U);
        context.r4 = int32_t(header);
        struct Scope {
            const recomp_context* previous = conversion_query;
            explicit Scope(const recomp_context* ctx) { conversion_query = ctx; }
            ~Scope() { conversion_query = previous; }
        } scope(&context);

        func_80041F68(rdram, &context);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = TWINE_MEM_BU(i, data);
    }
    explicit Resources(std::span<const uint8_t> rom) : arena(8 * 1024 * 1024) {

        for (size_t i = 0; i < 0xD6BF0; ++i)
            arena[(0x460 + i) ^ 3] = rom[0x1060 + i];
    }
    std::vector<uint8_t> unpack(std::span<const uint8_t> input) {
        if (input.size() < 12 || (word(input, 0) != 0x45444C81 && word(input, 0) != 0x45444C82))
            return {input.begin(), input.end()};
        const auto compressed = word(input, 4), size = word(input, 8);
        constexpr uint32_t source = 0x80200000U, destination = 0x80400000U;
        if (compressed != input.size() || compressed > 1024 * 1024 || size > 2 * 1024 * 1024 || !size)
            throw std::runtime_error("Invalid native EDL texture extent");
        auto *rdram = arena.data();
        for (size_t i = 0; i < input.size(); ++i)
            TWINE_MEM_B(i, source) = input[i];
        recomp_context context{};
        context.r29 = int32_t(0x807FF000U);
        context.r4 = int32_t(destination);
        context.r5 = int32_t(source);
        func_80014A0C(rdram, &context);
        if (!context.r2)
            throw std::runtime_error("Native EDL texture decompression failed");
        std::vector<uint8_t> result(size);
        for (size_t i = 0; i < size; ++i)
            result[i] = TWINE_MEM_BU(i, destination);
        return result;
    }
    std::map<unsigned, std::map<unsigned, std::set<unsigned>>> palette_pairs(std::span<const uint8_t> rom) {
        constexpr size_t base = 0x54D910;
        std::map<unsigned, std::map<unsigned, std::set<unsigned>>> result;
        std::set<uint64_t> seen;
        for (unsigned bank = 0; bank < 507; ++bank) {
            const auto header = extent(rom, base + word(rom, 0xC1380 + bank * 16), 48);
            const auto count = word(header, 4);
            if (count > 4096)
                throw std::runtime_error("Invalid model record count");
            const auto models = extent(rom, base + word(header, 0), size_t(count) * 68);
            for (unsigned model = 0; model < count; ++model) {
                const auto offset = word(models, model * 68 + 36);
                const auto bankPointer = word(models, model * 68 + 40);
                if (bankPointer < 0x800C2740U || bankPointer > 0x800C2B34U || (bankPointer & 1))
                    throw std::runtime_error("Invalid native material bank reference");
                const unsigned sourceBank = half(rom, bankPointer - 0x80000000U + 0xC00);
                if (sourceBank >= 507)
                    throw std::runtime_error("Invalid material bank index");
                if (!seen.insert((uint64_t(sourceBank) << 32) | offset).second)
                    continue;
                const auto sourceHeader = extent(rom, base + word(rom, 0xC1380 + sourceBank * 16), 48);
                const auto geometry = extent(rom, base + offset, 32);
                const auto length = half(geometry, 28);
                if (!length)
                    continue;
                const auto commands = unpack(extent(rom, base + word(geometry, 24), length));
                const auto image_count = word(sourceHeader, 28), palette_count = word(sourceHeader, 20);
                if (image_count > 256 || palette_count > 256)
                    throw std::runtime_error("Invalid native material bank extent");
                collect_material_pairs(arena, commands,
                    extent(rom, base + word(sourceHeader, 24), size_t(image_count) * 12),
                    extent(rom, base + word(sourceHeader, 16), size_t(palette_count) * 8), result[sourceBank]);
            }
        }
        return result;
    }
    void tiles(const BankImage &image, const std::function<void(NativeTile &&)> &consume) {
        constexpr uint32_t pixels = 0x80200000U, palette = 0x80300000U, record = 0x80301000U,
                           command = 0x80303000U, output = 0x80400000U, stack = 0x807FF000U;
        auto *rdram = arena.data();
        for (size_t i = 0; i < image.pixels.size(); ++i)
            TWINE_MEM_B(i, pixels) = image.pixels[i];
        for (unsigned i = 0; i < 8; ++i)
            TWINE_MEM_B(image.pixels.size() + i, pixels) = 0;
        for (size_t i = 0; i < image.palette.size(); ++i)
            TWINE_MEM_B(i, palette) = image.palette[i];
        TWINE_MEM_H(0, record) = image.flags;
        TWINE_MEM_H(2, record) = image.width - 1;
        TWINE_MEM_H(4, record) = image.height - 1;
        TWINE_MEM_H(6, record) = 0x100;
        TWINE_MEM_W(8, record) = pixels;
        TWINE_MEM_W(0, command) = 0;
        for (const bool intensity : {false, true}) {
            recomp_context context{};
            context.r29 = int32_t(stack);
            TWINE_MEM_W(0, 0x8010A510U) = output;

            context.r4 = int32_t(command);
            context.r5 = intensity ? 5 : 1;
            context.r6 = int32_t(record);
            context.r7 = 0;
            TWINE_MEM_W(16, stack) = 1;
            TWINE_MEM_W(20, stack) = int32_t(command + 8);
            func_80032978(rdram, &context);
            const uint32_t end = TWINE_MEM_W(0, 0x8010A510U);
            if (end < output || end - output > 65536 || (end - output) % 8)
                throw std::runtime_error("Invalid native texture command extent");
            NativeTile tile;
            tile.tlut = intensity ? G_TT_NONE : G_TT_RGBA16;
            auto alternate = tile.tmem;
            alternate.fill(0xA7);
            if (!intensity)
                for (unsigned i = 0; i < image.palette.size() / 2; ++i)
                    for (unsigned n = 0; n < 8; ++n)
                        tile.tmem[2048 + i * 8 + n] = alternate[2048 + i * 8 + n] = image.palette[i * 2 + (n & 1)];
            std::array<RT64::LoadTile, 8> descriptors{};

            for (auto &d : descriptors) {
                d.fmt = 2;
                d.siz = 2;
            }
            uint32_t address = 0, sourceWidth = 0, sourceSize = 0;
            bool loaded = false;
            const auto sample = [&] {
                tile.tile = descriptors[0];
                const auto dimension = [](unsigned low, unsigned high, unsigned mode, unsigned mask) {
                    const int extent =
                        (!mask || (mode & G_TX_CLAMP)) ? std::max((int(high) - int(low) + 4) / 4, 1) : 0xFFFF;
                    return unsigned(std::min(extent, mask ? 1 << mask : 0xFFFF));
                };
                tile.width = dimension(tile.tile.uls, tile.tile.lrs, tile.tile.cms, tile.tile.masks);
                tile.height = dimension(tile.tile.ult, tile.tile.lrt, tile.tile.cmt, tile.tile.maskt);
                if (!loaded || !tile.tile.line || tile.width > 256 || tile.height > 256 ||
                    RT64::TMEMHasher::requiresRawTMEM(tile.tile, tile.width, tile.height, tile.tlut))
                    return;
                tile.hash = RT64::TMEMHasher::hash(tile.tmem.data(), tile.tile, tile.width, tile.height, tile.tlut, 6);

                if (tile.hash !=
                    RT64::TMEMHasher::hash(alternate.data(), tile.tile, tile.width, tile.height, tile.tlut, 6))
                    return;
                consume(NativeTile(tile));
            };
            for (uint32_t pc = output; pc < end; pc += 8) {
                const uint32_t a = TWINE_MEM_W(0, pc), b = TWINE_MEM_W(4, pc), op = a >> 24;
                const unsigned index = (b >> 24) & 7;
                auto &d = descriptors[index];
                if (op == 0xFD) {
                    address = b;
                    sourceWidth = (a & 4095) + 1;
                    sourceSize = (a >> 19) & 3;
                } else if (op == 0xF5) {
                    d.fmt = (a >> 21) & 7;
                    d.siz = (a >> 19) & 3;
                    d.line = (a >> 9) & 511;
                    d.tmem = a & 511;
                    d.palette = (b >> 20) & 15;
                    d.cms = (b >> 8) & 3;
                    d.masks = (b >> 4) & 15;
                    d.shifts = b & 15;
                    d.cmt = (b >> 18) & 3;
                    d.maskt = (b >> 14) & 15;
                    d.shiftt = (b >> 10) & 15;
                } else if (op == 0xF2) {
                    d.uls = (a >> 12) & 4095;
                    d.ult = a & 4095;
                    d.lrs = (b >> 12) & 4095;
                    d.lrt = b & 4095;
                } else if (op == 0xF3 || op == 0xF4) {
                    const unsigned left = (a >> 12) & 4095, top = a & 4095, right = (b >> 12) & 4095, bottom = b & 4095;
                    const bool block = op == 0xF3;
                    const unsigned rows = block ? 1 : ((bottom >> 2) - (top >> 2) + 1);
                    const unsigned words =
                        block ? ((right - left) >> (4 - d.siz)) + 1 : (((right >> 2) - (left >> 2)) >> (4 - d.siz)) + 1;
                    const unsigned srcStride = (sourceWidth << sourceSize) >> 1;
                    const uint32_t start = address + (((block ? left : left >> 2) << sourceSize) >> 1) +
                                           (block ? top : top >> 2) * srcStride;
                    unsigned swap = 0, counter = 0;
                    for (unsigned y = 0; y < rows; ++y) {
                        unsigned dest = (d.tmem * 8 + y * d.line * 8) & 4095;
                        for (unsigned w = 0; w < words; ++w) {
                            for (unsigned n = 0; n < 8; ++n) {
                                const uint32_t p = start + y * srcStride + w * 8 + n;
                                if (p < pixels || p - pixels >= image.pixels.size() + 8)
                                    throw std::runtime_error("Native texture load exceeds bank frame");
                                const auto v = TWINE_MEM_BU(0, p);
                                tile.tmem[(dest + n) ^ swap] = v;

                                alternate[(dest + n) ^ swap] = p - pixels < image.pixels.size() ? v : uint8_t(v ^ 0xA7);
                            }
                            if (block) {
                                counter += bottom;
                                while (counter >= 2048) {
                                    dest = (dest + d.line * 8) & 4095;
                                    counter -= 2048;
                                    swap ^= 4;
                                }
                            }
                            dest = (dest + 8) & 4095;
                        }
                        if (!block)
                            swap ^= 4;
                    }
                    loaded = true;
                }
            }
            sample();
        }
    }
};
}

void visit_bank_images(std::span<const uint8_t> rom, const std::function<void(BankImage &&)> &consume,
                       std::stop_token stop) {

    if (rom.size() != 33554432 || XXH3_64bits(rom.data(), rom.size()) != 0x20AC5D898372B3C7ULL)
        throw std::runtime_error("Texture preparation requires the supported TWINE North America revision 0 ROM");
    Resources resources(rom);
    if (stop.stop_requested())
        throw PreparationCancelled{};
    constexpr size_t base = 0x54D910, table = 0xC1380, banks = 507;
    auto allPairs = resources.palette_pairs(rom);
    for (unsigned bank = 0; bank < banks; ++bank) {
        if (stop.stop_requested())
            throw PreparationCancelled{};
        const auto header = extent(rom, base + word(rom, table + bank * 16), 48);
        const auto paletteCount = word(header, 20), imageCount = word(header, 28);
        if (paletteCount > 256 || imageCount > 256)
            throw std::runtime_error("Invalid native texture record count");
        const auto palettes = extent(rom, base + word(header, 16), paletteCount * 8);
        const auto images = extent(rom, base + word(header, 24), imageCount * 12);
        const auto paletteBytes = resources.unpack(extent(rom, base + word(header, 32), word(header, 36)));
        auto pixels = resources.unpack(extent(rom, base + word(header, 40), word(header, 44)));
        const auto &pairs = allPairs[bank];
        resources.convert_images(images, pixels);
        for (unsigned index = 0; index < imageCount; ++index) {
            const auto record = images.subspan(index * 12, 12);
            const unsigned width = half(record, 2) + 1U, height = half(record, 4) + 1U;

            if (width == 1 && height == 1)
                continue;
            const unsigned frames = std::max(1U, unsigned(record[6]));
            const unsigned bytes = width * height / ((record[0] & 1) ? 1 : 2);
            if (width > 1024 || height > 1024 || !bytes)
                throw std::runtime_error("Invalid native texture dimensions at " + std::to_string(bank) + ":" +
                                         std::to_string(index) + " " + std::to_string(width) + "x" +
                                         std::to_string(height));
            const auto data = extent(pixels, word(record, 8), size_t(bytes) * frames);
            std::span<const uint8_t> palette;
            if (index < paletteCount) {
                const auto p = palettes.subspan(index * 8, 8);
                palette = extent(paletteBytes, size_t(word(p, 4)) * 2, (unsigned(p[0]) + 1) * 2);
            }
            for (unsigned frame = 0; frame < frames; ++frame) {
                BankImage result{bank, index, frame, width, height, half(record, 0)};
                const auto source = data.subspan(frame * bytes, bytes);
                result.pixels.assign(source.begin(), source.end());
                result.palette.assign(palette.begin(), palette.end());
                if (const auto found = pairs.find(index); found != pairs.end()) {
                    result.material = true;
                    for (const auto alternate : found->second)
                        if (alternate != index && alternate != UINT32_MAX) {
                            const auto p = palettes.subspan(alternate * 8, 8);
                            const auto bytes = extent(paletteBytes, size_t(word(p, 4)) * 2, (unsigned(p[0]) + 1) * 2);
                            result.alternate_palettes.emplace_back(bytes.begin(), bytes.end());
                        }
                }
                consume(std::move(result));
            }
        }
    }
}

void visit_bank_tiles(std::span<const uint8_t> rom, const std::function<void(NativeTile &&)> &consume,
                      std::stop_token stop) {
    if (rom.size() != 33554432 || XXH3_64bits(rom.data(), rom.size()) != 0x20AC5D898372B3C7ULL)
        throw std::runtime_error("Unsupported texture source ROM");
    Resources emitter(rom);
    if (stop.stop_requested())
        throw PreparationCancelled{};
    visit_bank_images(
        rom,
        [&](BankImage &&image) {

            if (!image.material)
                return;
            emitter.tiles(image, consume);
            for (auto &palette : image.alternate_palettes) {
                image.palette = std::move(palette);
                emitter.tiles(image, consume);
            }
        },
        stop);
}
}
