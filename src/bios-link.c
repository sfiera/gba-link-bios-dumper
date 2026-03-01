#include <string.h>
#include "bios_dumper.gba.h"
#include "crc32.h"
#include "gfx.h"
#include "link.h"

#define SRAM ((volatile u8*)0x0e000000)

static char savetype[] = "SRAM_V123";  // So that save tools can figure out the format

__attribute__((section(".bss"))) u8 out[0x4000];

static bool send_rom() {
    u32 size = (bios_dumper_gba_size + 0xF) & ~0xF;
    return link_multiboot_send(bios_dumper_gba, size);
}

static bool recv_bios() {
    link_start();
    while (true) {
        if (!link_send(0x0200)) {
            continue;
        } else if (REG_SIOMULTI[1] == 0x0201) {
            break;
        }
    }
    if (!link_send(0x0202)) {
        return false;
    }
    u16* data = (u16*)out;
    u32  size = 0x2000;
    while (size) {
        if (!link_send(0x0203)) {
            return false;
        }
        *(data++) = REG_SIOMULTI[1];
        --size;
        if ((size % 0x100) == 0) {
            gfx_toggle((0x1f00 - size) >> 8);
        }
    }
    link_stop();
    return true;
}

int main() {
    crc32_init();
    gfx_init();

    if (strcmp(savetype, "SRAM_V123") != 0) {
        gfx_show(gfx_failure);
        goto wait;
    }

    *SRAM = 0x55;
    if (*SRAM != 0x55) {
        gfx_show(gfx_failure);
        goto wait;
    }

    gfx_show(gfx_upload);
    if (!send_rom()) {
        gfx_show(gfx_failure);
        goto wait;
    }

    gfx_show(gfx_download);
    if (!recv_bios()) {
        gfx_show(gfx_failure);
        goto wait;
    }

#ifdef BIOS_WRITE_SRAM
    for (size_t i = 0; i < sizeof(out); ++i) {
        SRAM[i] = out[i];
    }
#endif

    u32 crc = -1;
    crc     = crc32(crc, out, 0x4000);
    crc     = crc32(crc, (const u8*)"\x00\x40", 2);
    gfx_show((crc == 0xe5016a5c) ? gfx_success : gfx_failure);

wait:
    while (1) {
    }
}
