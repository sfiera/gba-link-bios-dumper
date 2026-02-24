#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <stdio.h>
#include "LinkCable.hpp"

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

LinkCable* linkCable;

void send_rom() {
    irqSet((irqMASK)IRQ_VBLANK, LINK_CABLE_ISR_VBLANK);
    irqEnable(IRQ_VBLANK);
    irqSet((irqMASK)IRQ_SERIAL, LINK_CABLE_ISR_SERIAL);
    irqEnable(IRQ_SERIAL);
    irqSet((irqMASK)IRQ_TIMER3, LINK_CABLE_ISR_TIMER);
    irqEnable(IRQ_TIMER3);
    linkCable->activate();

    const u8* data = out;
    const u8* const end = out + 0x4000;

    while (true) {
        linkCable->sync();
        if (linkCable->isConnected()
            && linkCable->currentPlayerId() == 1) {
            u16 message = 0x0100 | *data;
            iprintf("msg: $%04hx\n", message);
            if (linkCable->send(message)) {
                ++data;
            }
            if (data == end) {
                return;
            }
        }
    }
}

int main() {
    linkCable = new LinkCable;
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
