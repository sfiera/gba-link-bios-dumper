#include <gba_systemcalls.h>

__attribute__((section(".bss"))) u8 out[0x4000];

volatile u16* REG_RCNT         = (volatile u16*)(0x04000134);
volatile u16* REG_SIOCNT       = (volatile u16*)(0x04000128);
volatile u16* REG_SIOMLT_SEND  = (volatile u16*)(0x0400012A);

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

void send_rom() {
    *REG_RCNT         = 0x0000;
    *REG_SIOMLT_SEND  = 0x0000;
    *REG_SIOCNT       = 0x2003;

    const u8* data = out;
    const u8* const end = out + 0x4000;

    while (true) {
        u16 message = 0x0100 | *data;

        *REG_SIOMLT_SEND = message;
        *REG_SIOCNT |= 1 << 7;
        while ((*REG_SIOCNT >> 7) & 1) {}

        if (!((*REG_SIOCNT >> 3) & 1) || ((*REG_SIOCNT >> 6) & 1)) {
            continue;
        }
        ++data;
        if (data == end) {
            return;
        }
    }
}

int main() {
	dump();
    send_rom();
	while (1) {
		VBlankIntrWait();
	}
}
