#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bios_dumper.gba.h"
#include "LinkCableMultiboot.hpp"
#ifdef BIOS_CALC_SHA256
#include "Sha256.h"
#endif

char savetype[] = "SRAM_V123"; // So that save tools can figure out the format

volatile u16* REG_RCNT         = (volatile u16*)(0x04000134);
volatile u16* REG_SIOCNT       = (volatile u16*)(0x04000128);
volatile u16* REG_SIOMLT_SEND  = (volatile u16*)(0x0400012A);
volatile u16* REG_SIOMULTI     = (volatile u16*)(0x04000120);

IWRAM_DATA u8 out[0x4000];

#ifdef BIOS_CALC_SHA256
const u8 sha256_checksum_agb[SHA256_DIGEST_SIZE] = {
	0xfd, 0x25, 0x47, 0x72, 0x4b, 0x50, 0x5f, 0x48,
	0x7e, 0x6d, 0xcb, 0x29, 0xec, 0x2e, 0xcf, 0xf3,
	0xaf, 0x35, 0xa8, 0x41, 0xa7, 0x7a, 0xb2, 0xe8,
	0x5f, 0xd8, 0x73, 0x50, 0xab, 0xd3, 0x65, 0x70
};

const u8 sha256_checksum_ntr[SHA256_DIGEST_SIZE] = {
	0x78, 0x2e, 0xb3, 0x89, 0x42, 0x37, 0xec, 0x6a,
	0xa4, 0x11, 0xb7, 0x8f, 0xfe, 0xe1, 0x90, 0x78,
	0xba, 0xcf, 0x10, 0x41, 0x38, 0x56, 0xd3, 0x3c,
	0xda, 0x10, 0xb4, 0x4f, 0xd9, 0xc2, 0x85, 0x6b
};

IWRAM_DATA CSha256 sha256_data;

void calcSha256(void) {
	u8 checksum[SHA256_DIGEST_SIZE];
	int i;

	iprintf("Calculating SHA256...\n");

	Sha256_Init(&sha256_data);
	Sha256_Update(&sha256_data, out, sizeof(out));
	Sha256_Final(&sha256_data, checksum);

	iprintf("SHA256: ");
	for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
		iprintf("%02x", checksum[i]);
	}
	iprintf("\nCPU %s\n",
		!memcmp(checksum, sha256_checksum_agb, SHA256_DIGEST_SIZE) ? "AGB" :
		!memcmp(checksum, sha256_checksum_ntr, SHA256_DIGEST_SIZE) ? "NTR" : "???");
	iprintf("\n");
}
#endif

bool send_rom() {
    u32 size = (bios_dumper_gba_size + 0xF) & ~0xF;
    iprintf("Sending %ld bytes\n", size);
    return LinkCableMultiboot::sendRom(bios_dumper_gba, size);
}

bool recv_bios() {
    *REG_RCNT         = 0x0000;
    *REG_SIOMLT_SEND  = 0x0000;
    *REG_SIOCNT       = 0x2003;

    u8* data = out;
    u8* const end = out + 0x4000;

    while (true) {
        *REG_SIOMLT_SEND = 0x0200;
        *REG_SIOCNT |= 1 << 7;
        while ((*REG_SIOCNT >> 7) & 1) {}

        if (!((*REG_SIOCNT >> 3) & 1) || ((*REG_SIOCNT >> 6) & 1)) {
            continue;
        }
        u16 message = REG_SIOMULTI[1];
        if ((message & 0xFF00) == 0x0100) {
            *(data++) = message & 0xFF;
            if (((data - out) % 0x400) == 0) {
                iprintf(".");
            }
            if (data == end) {
                iprintf("\n");
                return true;
            }
        }
    }
}

int main() {
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

    iprintf("Sent, receiving bios\n");
    recv_bios();

#ifdef BIOS_WRITE_SRAM
	for (size_t i = 0; i < sizeof(out); ++i) {
		((vu8*) SRAM)[i] = out[i];
	}
#endif

#ifdef BIOS_CALC_SHA256
	calcSha256();
#endif

wait:
	while (1) {
		VBlankIntrWait();
	}
}
