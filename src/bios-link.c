#include <gba_console.h>
#include <gba_systemcalls.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bios_dumper.gba.h"
#include "crc32.h"
#include "link.h"

char savetype[] = "SRAM_V123"; // So that save tools can figure out the format

__attribute__((section(".bss"))) u8 out[0x4000];

static bool send_rom() {
    u32 size = (bios_dumper_gba_size + 0xF) & ~0xF;
    iprintf("Sending %ld bytes\n", size);
    return link_multiboot_send(bios_dumper_gba, size);
}

static void recv_bios() {
    link_start();
    u8* data = out;
    while (data != out + 0x4000) {
        if (!link_send(0x0200)) {
            continue;
        }
        u16 message = REG_SIOMULTI[1];
        if ((message & 0xFF00) == 0x0100) {
            *(data++) = message & 0xFF;
            if (((data - out) % 0x400) == 0) {
                iprintf(".");
            }
        }
    }
    iprintf("\n");
    link_stop();
}

static void wait(u32 verticalLines) {
    u32 count = 0;
    u32 vCount = REG_VCOUNT;

    while (count < verticalLines) {
        if (REG_VCOUNT != vCount) {
            count++;
            vCount = REG_VCOUNT;
        }
    };
}

int main() {
    crc32_init();
	consoleDemoInit();

	if (strcmp(savetype, "SRAM_V123") != 0) {
		iprintf("Cartridge error, continuing anyway\n");
	}
	*(vu8*) SRAM = 0x55;
	if (*(vu8*) SRAM != 0x55) {
		iprintf("Fatal SRAM error!\n");
		return 1;
	}
    
    if (!send_rom()) {
        iprintf("Failed to send\n");
        goto wait;
    }
    iprintf("Sent\n");

    // wait 5s (60 frames)
    // TODO: perform handshake instead of just waiting
    wait(5 * 60 * 228);

    iprintf("Receiving bios\n");
    recv_bios();

#ifdef BIOS_WRITE_SRAM
	for (size_t i = 0; i < sizeof(out); ++i) {
		((vu8*) SRAM)[i] = out[i];
	}
#endif

    u32 crc = -1;
    crc = crc32(crc, out, 0x4000);
    crc = crc32(crc, (const u8*)"\x00\x40", 2);
    iprintf("CRC32: %08lx\n", crc);
    iprintf((crc == 0xe5016a5c) ? "CPU AGB\n" : "CPU ???\n");

wait:
	while (1) {
		VBlankIntrWait();
	}
}
