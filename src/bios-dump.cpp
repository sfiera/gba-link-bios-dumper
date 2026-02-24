#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <stdio.h>
#include "LinkRawCable.hpp"

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

LinkRawCable* linkRawCable;

void send_rom() {
    irqSet((irqMASK)IRQ_SERIAL, LINK_RAW_CABLE_ISR_SERIAL);
    irqEnable(IRQ_SERIAL);
    linkRawCable->activate();

    const u8* data = out;
    const u8* const end = out + 0x4000;

    while (true) {
        u16 message = 0x0100 | *data;
        if (linkRawCable->transfer(message).playerId < 0) {
            continue;
        }
        ++data;
        if (((data - out) % 0x400) == 0) {
            iprintf(".");
        }
        if (data == end) {
            iprintf("\n");
            return;
        }
    }
}

int main() {
    linkRawCable = new LinkRawCable;
    irqInit();
	consoleDemoInit();

	u32 checksum = BiosCheckSum();
	iprintf("BIOS Checksum: %08lX\nCPU %s\n", checksum,
		checksum == 0xBAAE187F ? "AGB" :
		checksum == 0xBAAE1880 ? "NTR" : "???");

	dump();
	iprintf("Done dumping!\n");

    send_rom();
	iprintf("Done sending!\n");

	while (1) {
		VBlankIntrWait();
	}
}
