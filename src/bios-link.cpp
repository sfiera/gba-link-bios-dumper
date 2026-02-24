#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bios_dumper.gba.h"
#include "LinkRawCable.hpp"
#include "LinkCableMultiboot.hpp"
#ifdef BIOS_CALC_SHA256
#include "Sha256.h"
#endif

char savetype[] = "SRAM_V123"; // So that save tools can figure out the format

IWRAM_DATA u8 out[0x4000];

void dump(void) {
	__asm__ __volatile__(
		"mov r0, #0 \n"
		"ldr r11, =out \n"
		"orr r10, r11, #0x4000 \n"
		"mov r1, r11 \n"
		"ldr r12, =0xC14 \n" // CpuFastSet core
		"add lr, pc, #4 \n"
		"push {r4-r10,lr} \n"
		"bx r12 \n"
		"mov r0, #0xE000000 \n"
	: : : "r0", "r1", "r2", "r3", "r10", "r11", "r12", "lr", "memory");
}

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

IWRAM_DATA LinkRawCable* linkRawCable;
IWRAM_DATA LinkCableMultiboot* linkCableMultiboot;

LinkCableMultiboot::Result send_rom() {
    irqSet(IRQ_VBLANK, LINK_CABLE_MULTIBOOT_ASYNC_ISR_VBLANK);
    irqEnable(IRQ_VBLANK);
    irqSet(IRQ_SERIAL, LINK_CABLE_MULTIBOOT_ASYNC_ISR_SERIAL);
    irqEnable(IRQ_SERIAL);

    u32 size = (bios_dumper_gba_size + 0xF) & ~0xF;
    iprintf("Sending %ld bytes\n", size);
    if (!linkCableMultiboot->sendRom(bios_dumper_gba, size)) {
        return linkCableMultiboot->getDetailedResult();
    }
    return LinkCableMultiboot::Result::NONE;
}

bool recv_bios() {
    irqSet((irqMASK)IRQ_SERIAL, LINK_RAW_CABLE_ISR_SERIAL);
    irqEnable(IRQ_SERIAL);
    linkRawCable->activate();

    u8* data = out;
    u8* const end = out + 0x4000;

    while (true) {
        u16 message = linkRawCable->transfer(0x0200).data[1];
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
    int mb, pct;
    linkRawCable = new LinkRawCable;
    linkCableMultiboot = new LinkCableMultiboot;
    irqInit();

	consoleDemoInit();

	if (strcmp(savetype, "SRAM_V123") != 0) {
		iprintf("Cartridge error, continuing anyway\n");
	}
	*(vu8*) SRAM = 0x55;
	if (*(vu8*) SRAM != 0x55) {
		iprintf("Fatal SRAM error!\n");
		return 1;
	}
    
    LinkCableMultiboot::Result result = send_rom();
    if (result != LinkCableMultiboot::Result::NONE) {
        iprintf("Failed to send: %d\n", (int)result);
        goto wait;
    }
    pct = 0;
    while (linkCableMultiboot->isSending()) {
        VBlankIntrWait();
        if (linkCableMultiboot->getPercentage() >= pct + 10) {
            pct += 10;
            iprintf("%d%%...", pct);
        }
	}
    if (linkCableMultiboot->getDetailedResult() != LinkCableMultiboot::Result::SUCCESS) {
        iprintf("Failed to send: %d\n", (int)result);
        goto wait;
    }

    Link::_MultiBootParam multiBootParameters;
    multiBootParameters.client_data[0] = 0xFF;
    multiBootParameters.client_data[1] = 0xFF;
    multiBootParameters.client_data[2] = 0xFF;
    multiBootParameters.palette_data = LINK_CABLE_MULTIBOOT_PALETTE_DATA;
    multiBootParameters.client_bit = 0;
    multiBootParameters.boot_srcp = (u8*)bios_dumper_gba + 0xC0;
    multiBootParameters.boot_endp = (u8*)bios_dumper_gba + ((bios_dumper_gba_size + 0xF) & ~0xF);

    // 9. Call SWI 0x25, with r0 set to the address of the multiboot parameter
    // structure and r1 set to the communication mode (0 for normal, 1 for
    // MultiPlay).
    mb = Link::_MultiBoot(&multiBootParameters, 1);
    iprintf("MultiBoot: %d\n", mb);

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
