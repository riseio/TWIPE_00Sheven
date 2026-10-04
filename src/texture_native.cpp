#include "texture_native.hpp"
#include <stdexcept>
#include "shared/rt64_f3d_defines.h"

namespace twine::textures {
Image decode_native(const NativeTile& s) {
    const auto& tile = s.tile;
    if (!s.width || !s.height || s.width > 256 || s.height > 256 ||
        !tile.line || tile.line > 511 || tile.tmem > 511 || tile.siz > 3 ||
        tile.fmt > 4 || tile.fmt == 1 || tile.palette > 15 ||
        (s.tlut != G_TT_NONE && s.tlut != G_TT_RGBA16 && s.tlut != G_TT_IA16))
        throw std::runtime_error("Unsupported native texture reconstruction layout");
    const bool split = tile.fmt == 0 && tile.siz == 3;
    const unsigned mask = (split || s.tlut) ? 2047 : 4095;
    const unsigned stride = tile.line * 8;
    Image image{s.width, s.height, {}};
    image.rgba.resize(size_t(s.width) * s.height * 4);
    for (unsigned y = 0; y < s.height; ++y) for (unsigned x = 0; x < s.width; ++x) {

        const auto read = [&](unsigned relative, unsigned high = 0) {
            const unsigned row = relative / stride * stride;
            const unsigned column = relative - row;
            const unsigned p = tile.tmem * 8 + row +
                (((column / 4) ^ (y & 1)) * 4) + (column & 3);
            return s.tmem[((p & mask) | high) & 4095];
        };
        const unsigned p = y * stride + ((x << (split ? 2 : tile.siz)) >> 1);
        const unsigned a = read(p), b = read(p + 1), nibble = (a >> ((x & 1) ? 0 : 4)) & 15;
        auto* out = image.rgba.data() + (size_t(y) * s.width + x) * 4;
        const auto rgba16 = [&](unsigned v) {
            out[0] = uint8_t(((v >> 11) * 255 + 15) / 31);
            out[1] = uint8_t((((v >> 6) & 31) * 255 + 15) / 31);
            out[2] = uint8_t((((v >> 1) & 31) * 255 + 15) / 31);
            out[3] = (v & 1) ? 255 : 0;
        };
        if (s.tlut) {
            const unsigned address = (2048 + (tile.siz == 0 ? tile.palette * 128 + nibble * 8 : a * 8)) & 4095;
            const unsigned value = unsigned(s.tmem[address]) * 256 + s.tmem[(address + 1) & 4095];
            if (s.tlut == G_TT_RGBA16) rgba16(value);
            else { out[0] = out[1] = out[2] = uint8_t(value >> 8); out[3] = uint8_t(value); }
        }
        else if (tile.siz == 0) {
            if (tile.fmt == 3) {
                out[0] = out[1] = out[2] = uint8_t(((nibble >> 1) * 255 + 3) / 7);
                out[3] = (nibble & 1) ? 255 : 0;
            }
            else out[0] = out[1] = out[2] = out[3] = uint8_t(tile.fmt == 2 ? tile.palette * 16 + nibble : nibble * 17);
        }
        else if (tile.siz == 1) {
            out[0] = out[1] = out[2] = uint8_t(tile.fmt == 3 ? (a >> 4) * 17 : a);
            out[3] = uint8_t(tile.fmt == 3 ? (a & 15) * 17 : a);
        }
        else if (tile.siz == 2) {
            if (tile.fmt == 0) rgba16(a * 256 + b);
            else {
                out[0] = out[2] = uint8_t(a);
                out[1] = uint8_t(tile.fmt == 3 ? a : b); out[3] = uint8_t(b);
            }
        }
        else {
            const unsigned c = read(split ? p : p + 2, split ? 2048 : 0);
            const unsigned d = read(split ? p + 1 : p + 3, split ? 2048 : 0);
            if (split) { out[0] = uint8_t(a); out[1] = uint8_t(b); out[2] = uint8_t(c); out[3] = uint8_t(d); }
            else {
                out[0] = out[2] = uint8_t((x & 1) ? a : c);
                out[1] = out[3] = uint8_t((x & 1) ? b : d);
            }
        }
    }
    return image;
}
}
