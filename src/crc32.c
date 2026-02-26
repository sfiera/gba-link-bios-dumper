#include "crc32.h"

static __attribute__((section(".bss"))) u32 crc32_table[0x100];

void crc32_init() {
    for (u32 i = 0; i < 256; ++i) {
        u32 crc = i << 24;
        for (u32 j = 0; j < 8; ++j) {
            crc = (crc << 1) ^ ((crc >> 31) ? 0x04c11db7 : 0);
        }
        crc32_table[i] = crc;
    }
}

u32 crc32(u32 init, const u8* data, u32 size) {
    u32 crc = ~init;
    for (u32 i = 0; i < size; ++i) {
        crc = crc32_table[(crc >> 24) ^ data[i]] ^ (crc << 8);
    }
    return ~crc;
}
