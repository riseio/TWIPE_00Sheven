#include <stdio.h>
#include <stdlib.h>
#include "twine_recomp.h"

void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
void unload_overlays(int32_t ram_addr, uint32_t size);

void kmcWritebackDCache(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;

}

void twine_register_overlay(uint8_t* rdram, recomp_context* ctx) {
    uint32_t rom_start = (uint32_t)ctx->r5;
    uint32_t rom_end = (uint32_t)ctx->r6;
    if (rom_end <= rom_start || rom_end - rom_start > 0x40000U) {
        fprintf(stderr, "Invalid overlay ROM range: 0x%08X-0x%08X\n", rom_start, rom_end);
        exit(EXIT_FAILURE);
    }

    unload_overlays((int32_t)ctx->r4, 0x40000);
    twine_cut_render_tags();
    load_overlays(rom_start, (int32_t)ctx->r4, rom_end - rom_start);
}
