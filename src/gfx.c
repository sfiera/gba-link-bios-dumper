#include "gfx.h"

const u32 gfx_failure[7] = {
    0x0400040,  //
    0x2fc06f8,  //
    0x03fef90,  //
    0x007fd00,  //
    0x06fbfc0,  //
    0x2f903f8,  //
    0x0100010,  //
};

const u32 gfx_success[7] = {
    0x0000000,  //
    0x0400000,  //
    0x2fc0000,  //
    0x03fc040,  //
    0x003fef8,  //
    0x0003f90,  //
    0x0000100,  //
};

const u32 gfx_upload[7] = {
    0x0000000,  //
    0x0004000,  //
    0x006fc00,  //
    0x06fbfc0,  //
    0x0390390,  //
    0x0000000,  //
    0x0000000,  //
};

const u32 gfx_download[7] = {
    0x0000000,  //
    0x0000000,  //
    0x06c06c0,  //
    0x03fef90,  //
    0x003f900,  //
    0x0001000,  //
    0x0000000,  //
};

static const u32 tile_bit[4 * 8] = {
    0x11111111, 0x00000001, 0x00000000, 0x10000000,  //
    0x01111110, 0x00000011, 0x00000000, 0x11000000,  //
    0x00111100, 0x00000111, 0x00000000, 0x11100000,  //
    0x00011000, 0x00001111, 0x00000000, 0x11110000,  //
    0x00000000, 0x00001111, 0x00011000, 0x11110000,  //
    0x00000000, 0x00000111, 0x00111100, 0x11100000,  //
    0x00000000, 0x00000011, 0x01111110, 0x11000000,  //
    0x00000000, 0x00000001, 0x11111111, 0x10000000,  //
};

void gfx_init() {
    REG_DISPCNT = 0x0080;  // Enable forced blank

    u32* tileset = (u32*)0x06000000;
    for (int i = 0; i < 16; ++i) {
        const u32* data = tile_bit;
        for (int j = 0; j < 8; ++j) {
            for (int k = 1; k != 0x10; k <<= 1) {
                if (i & k) {
                    *tileset |= *data;
                }
                ++data;
            }
            ++tileset;
        }
    }

    u16* bgpal = (u16*)0x05000000;
    bgpal[0]   = (31 << 10) | (31 << 5) | (31 << 0);
    bgpal[1]   = (0 << 10) | (0 << 5) | (0 << 0);

    REG_BG0CNT  = 0x0800;  // 32x32 map at 8, 4bpp chars at 0
    REG_BG0HOFS = -(240 - 8 * 9) / 2;
    REG_BG0VOFS = -(160 - 8 * 9) / 2;

    REG_DISPCNT = 0x0100;  // Mode 1 with BG0 only
}

void gfx_show(const u32* icon) {
    u16* tilemap = (u16*)0x06004000;
    tilemap += 0x21;
    for (int i = 0; i < 7; ++i) {
        u32 row = *(icon++);
        for (int j = 0; j < 7; ++j) {
            *(tilemap++) = row & 0xF;
            row >>= 4;
        }
        tilemap += 25;
    }
}

static const u8 positions[32] = {
    0x04, 0x05, 0x06, 0x07,                          //
    0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78,  //
    0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81,  //
    0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10,  //
    0x00, 0x01, 0x02, 0x03,                          //
};

void gfx_toggle(u8 pos) {
    pos          = positions[pos];
    u16* tilemap = (u16*)0x06004000;
    tilemap[((pos & 0xF0) << 1) | (pos & 0x0F)] ^= 0xF;
}
