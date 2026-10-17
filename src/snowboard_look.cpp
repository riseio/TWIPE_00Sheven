#include "twine_qol.hpp"
#include "twine_recomp.h"

extern "C" uint32_t twine_snowboard_free_look_active(uint8_t* rdram, uint32_t actor) {
    if (!rdram || twine::qol::settings().cold_reception_free_look !=
            twine::qol::ColdReceptionFreeLook::On ||
            (actor & 3U) || actor < 0x80000000U || actor > 0x807FFF80U) {
        return 0;
    }
    return TWINE_MEM_H(0, 0x80117386U) == 3 &&
        TWINE_MEM_BU(0, 0x800CEEB0U) == 0 &&
        TWINE_MEM_W(0, 0x80102ED8U) == 0 &&
        static_cast<uint32_t>(TWINE_MEM_W(0, 0x8010A500U)) == actor &&
        TWINE_MEM_H(0x7C, actor) == 2;
}
