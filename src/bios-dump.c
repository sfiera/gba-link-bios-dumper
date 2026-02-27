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
    const u8* data = out;
    while (data != (out + 0x4000)) {
        u16 message = 0x0100 | *data;
        if (link_send(message)) {
            ++data;
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
