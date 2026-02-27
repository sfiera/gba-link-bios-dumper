#include <gba_systemcalls.h>
#include "link.h"

__attribute__((section(".bss"))) u8 out[0x4000];

static void dump(void) {
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

static void send_rom() {
    link_start();
    while (true) {
        link_send(0x0201);
        if (REG_SIOMULTI[0] == 0x0202) {
            break;
        }
    }
    const u16* data = (const u16*)out;
    u32 size = 0x2000;
    while (size) {
        if (link_send(*data)) {
            ++data;
            --size;
        }
    }
    link_stop();
}

int main() {
	dump();
    send_rom();
	while (1) {
		VBlankIntrWait();
	}
}
