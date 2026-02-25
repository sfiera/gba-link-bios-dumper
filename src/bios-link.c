#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bios_dumper.gba.h"
#ifdef BIOS_CALC_SHA256
#include "Sha256.h"
#endif

char savetype[] = "SRAM_V123"; // So that save tools can figure out the format

volatile u16* REG_RCNT         = (volatile u16*)(0x04000134);
volatile u16* REG_SIOCNT       = (volatile u16*)(0x04000128);
volatile u16* REG_SIOMLT_SEND  = (volatile u16*)(0x0400012A);
volatile u16* REG_SIOMULTI     = (volatile u16*)(0x04000120);
volatile u16* REG_VCOUNT       = (volatile u16*)(0x04000006);

IWRAM_DATA u8 out[0x4000];

#define LINK_CABLE_MULTIBOOT_PALETTE_DATA 0b10010011

u32 randomSeed = 123;

inline int _qran() {
  randomSeed = 1664525 * randomSeed + 1013904223;
  return (randomSeed >> 16) & 0x7FFF;
}

inline int _qran_range(int min, int max) {
  return (_qran() * (max - min) >> 15) + min;
}

inline void wait(u32 verticalLines) {
  u32 count = 0;
  u32 vCount = *REG_VCOUNT;

  while (count < verticalLines) {
    if (*REG_VCOUNT != vCount) {
      count++;
      vCount = *REG_VCOUNT;
    }
  };
}

typedef struct {
  u32 reserved1[5];
  u8 handshake_data;
  u8 padding;
  u16 handshake_timeout;
  u8 probe_count;
  u8 client_data[3];
  u8 palette_data;
  u8 response_bit;
  u8 client_bit;
  u8 reserved2;
  u8* boot_srcp;
  u8* boot_endp;
  u8* masterp;
  u8* reserved3[3];
  u32 system_work2[4];
  u8 sendflag;
  u8 probe_target_bit;
  u8 check_wait;
  u8 server_type;
} _MultiBootParam;

#define LINK_INLINE inline __attribute__((always_inline))
LINK_INLINE s32 _MultiBoot(const _MultiBootParam* param,
                                   u32 mbmode) {
  register union {
    const _MultiBootParam* ptr;
    s32 res;
  } r0 asm("r0") = {param};
  register u32 r1 asm("r1") = mbmode;
  asm volatile inline("swi 0x25 << ((1f - . == 4) * -16); 1:"
                      : "+r"(r0), "+r"(r1)::"r3");
  return r0.res;
}

static const int FRAME_LINES = 228;
static const int INITIAL_WAIT_MIN_FRAMES = 4;
static const int INITIAL_WAIT_MAX_RANDOM_FRAMES = 10;
static const int INITIAL_WAIT_MIN_LINES =
    FRAME_LINES * INITIAL_WAIT_MIN_FRAMES;
static const int DETECTION_TRIES = 16;
static const int MAX_CLIENTS = 3;
static const int CLIENT_NO_DATA = 0xFF;
static const int CMD_HANDSHAKE = 0x6200;
static const int ACK_HANDSHAKE = 0x7200;
static const int CMD_CONFIRM_CLIENTS = 0x6100;
static const int CMD_SEND_PALETTE = 0x6300;
static const int HANDSHAKE_DATA = 0x11;
static const int CMD_CONFIRM_HANDSHAKE_DATA = 0x6400;
static const int ACK_RESPONSE = 0x7300;
static const int ACK_RESPONSE_MASK = 0xFF00;
static const int HEADER_SIZE = 0xC0;
static const int HEADER_PARTS = HEADER_SIZE / 2;

void start() {
  *REG_RCNT         = 0x0000;
  *REG_SIOMLT_SEND  = 0x0000;
  *REG_SIOCNT       = 0x2003;
}

void stop() {
  *REG_SIOMLT_SEND = 0;
  *REG_RCNT = (*REG_RCNT & ~(1 << 14)) | (1 << 15);
}

bool isBitHigh(u8 bit) { return (*REG_SIOCNT >> bit) & 1; }
void setBitHigh(u8 bit) { *REG_SIOCNT |= 1 << bit; }
void startTransfer() { setBitHigh(7); }
[[nodiscard]] bool allReady() { return isBitHigh(3); }
[[nodiscard]] bool hasError() { return isBitHigh(6); }
[[nodiscard]] bool isSending() { return isBitHigh(7); }

typedef struct {
  u32 data[4];
  int playerId;  // (-1 = unknown)
} Response;

Response transfer(u16 data) {
  *REG_SIOMLT_SEND = data;
  startTransfer();
  while (isSending()) {}
  Response response = {{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, -1};
  if (!allReady() || hasError())
    return response;

  for (u32 i = 0; i < 4; i++)
    response.data[i] = REG_SIOMULTI[i];
  response.playerId = (*REG_SIOCNT & (0b11 << 4)) >> 4;

  return response;
}

bool isResponseSameAsValue(Response response,
                           u8 clientMask,
                           u16 wantedValue,
                           u16 mask) {
  bool any = false;
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    u32 value = response.data[1 + i];
    if (value == 0xFFFF) {
      continue;
    }
    u8 clientBit = 1 << (i + 1);
    if ((clientMask & clientBit) && ((value & mask) != wantedValue)) {
      return false;
    }
    any = true;
  }
  return any;
}

bool confirmHandshakeData(_MultiBootParam* multiBootParameters) {
  // 8. Calculate the handshake_data byte and store it in the parameter
  // structure. This should be calculated as 0x11 + the sum of the three
  // client_data bytes. Send 0x64HH, where HH is the handshake_data.
  // The clients should respond 0x77GG, where GG is something unimportant.
  multiBootParameters->handshake_data =
      (HANDSHAKE_DATA + multiBootParameters->client_data[0] +
       multiBootParameters->client_data[1] +
       multiBootParameters->client_data[2]) %
      256;

  u16 data = CMD_CONFIRM_HANDSHAKE_DATA | multiBootParameters->handshake_data;
  Response response = transfer(data);
  return isResponseSameAsValue(response, multiBootParameters->client_bit,
                               ACK_RESPONSE, ACK_RESPONSE_MASK);
}

bool isResponseSameAsValueWithClientBit(Response response,
                                        u8 clientMask,
                                        u32 wantedValue) {
  bool any = false;
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    u32 value = response.data[1 + i];
    if (value == 0xFFFF) {
      continue;
    }
    u8 clientBit = 1 << (i + 1);
    if ((clientMask & clientBit) && (value != (wantedValue | clientBit))) {
      return false;
    }
    any = true;
  }
  return any;
}

bool detectClients(_MultiBootParam* multiBootParameters) {
  // 3. Send the word 0x6200 repeatedly until all detected clients respond
  // with 0x720X, where X is their client number (1-3). If they fail to do
  // this after 16 tries, delay 1/16s and go back to step 2. (*)
  bool success = false;
  for (u32 t = 0; t < DETECTION_TRIES; t++) {
    Response response = transfer(CMD_HANDSHAKE);
    multiBootParameters->client_bit = 0;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
      u32 value = response.data[1 + i];
      if (value == 0xFFFF) {
        continue;
      } else if ((value & 0xFFF0) != ACK_HANDSHAKE) {
        success = false;
        break;
      }
      u8 clientId = value & 0xF;
      u8 expectedClientId = 1 << (i + 1);
      if (clientId != expectedClientId) {
        success = false;
        break;
      }
      multiBootParameters->client_bit |= clientId;
      success = true;
    }

    if (success)
      break;
  }

  return success;
}

bool confirmClients(_MultiBootParam* multiBootParameters) {
  // 4. Fill in client_bit in the multiboot parameter structure (with
  // bits 1-3 set according to which clients responded). Send the word
  // 0x610Y, where Y is that same set of set bits.
  Response response =
      transfer(CMD_CONFIRM_CLIENTS | multiBootParameters->client_bit);

  // The clients should respond 0x7200.
  return isResponseSameAsValueWithClientBit(
      response, multiBootParameters->client_bit, ACK_HANDSHAKE);
}

bool sendHeader(_MultiBootParam* multiBootParameters, const u8* rom) {
  // 5. Send the cartridge header, 16 bits at a time, in little endian order.
  // After each 16-bit send, the clients will respond with 0xNN0X, where NN is
  // the number of words remaining and X is the client number. (Note that if
  // transferring in the single-client 32-bit mode, you still need to send
  // only 16 bits at a time).
  u16* headerOut = (u16*)rom;
  u32 remaining = HEADER_PARTS;
  while (remaining > 0) {
    Response response = transfer(*(headerOut++));
    if (!isResponseSameAsValueWithClientBit(
            response, multiBootParameters->client_bit, remaining << 8))
      return false;

    remaining--;
  }

  // 6. Send 0x6200, followed by 0x620Y again.
  // The clients should respond 0x000Y and 0x720Y.
  Response response;
  response = transfer(CMD_HANDSHAKE);
  if (!isResponseSameAsValueWithClientBit(response,
                                          multiBootParameters->client_bit, 0))
    return false;
  response = transfer(CMD_HANDSHAKE | multiBootParameters->client_bit);
  if (!isResponseSameAsValueWithClientBit(
          response, multiBootParameters->client_bit, ACK_HANDSHAKE))
    return false;

  return true;
}

bool sendPalette(_MultiBootParam* multiBootParameters) {
  // 7. Send 0x63PP repeatedly, where PP is the palette_data you have picked
  // earlier. Do this until the clients respond with 0x73CC, where CC is a
  // random byte. Store these bytes in client_data in the parameter structure.
  u16 data = CMD_SEND_PALETTE | LINK_CABLE_MULTIBOOT_PALETTE_DATA;

  for (u32 i = 0; i < DETECTION_TRIES; i++) {
    Response response = transfer(data);
    u8 successes = 0;

    for (u32 i = 0; i < MAX_CLIENTS; i++) {
      u32 value = response.data[1 + i];
      if (value == 0xFFFF) {
        continue;
      }
      u8 clientBit = 1 << (i + 1);
      if (!(multiBootParameters->client_bit & clientBit) ||
          ((value & ACK_RESPONSE_MASK) != ACK_RESPONSE)) {
        successes = 0;
        break;
      }
      multiBootParameters->client_data[i] = value & 0xFF;
      successes |= clientBit;
    }

    if (successes == multiBootParameters->client_bit) {
      return true;
    }
  }

  return false;
}

bool sendRom(const u8* rom, u32 romSize) {
  while (true) {
    stop();

    // (*) instead of 1/16s, waiting a random number of frames works better
    wait(INITIAL_WAIT_MIN_LINES +
               FRAME_LINES *
                   _qran_range(1, INITIAL_WAIT_MAX_RANDOM_FRAMES));

    // 1. Prepare a "Multiboot Parameter Structure" in RAM.
    _MultiBootParam multiBootParameters;
    multiBootParameters.client_data[0] = CLIENT_NO_DATA;
    multiBootParameters.client_data[1] = CLIENT_NO_DATA;
    multiBootParameters.client_data[2] = CLIENT_NO_DATA;
    multiBootParameters.palette_data = LINK_CABLE_MULTIBOOT_PALETTE_DATA;
    multiBootParameters.client_bit = 0;
    multiBootParameters.boot_srcp = (u8*)rom + HEADER_SIZE;
    multiBootParameters.boot_endp = (u8*)rom + romSize;

    // 2. Initiate a multiplayer communication session, using either Normal mode
    // for a single client or MultiPlay mode for multiple clients.
    start();

    if (detectClients(&multiBootParameters) &&
        confirmClients(&multiBootParameters) &&
        sendHeader(&multiBootParameters, rom) &&
        sendPalette(&multiBootParameters) &&
        confirmHandshakeData(&multiBootParameters)) {
      int result = _MultiBoot(&multiBootParameters, 1);
      stop();
      return result == 0;
    }
  }
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

bool send_rom() {
    u32 size = (bios_dumper_gba_size + 0xF) & ~0xF;
    iprintf("Sending %ld bytes\n", size);
    return sendRom(bios_dumper_gba, size);
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

#ifdef BIOS_CALC_SHA256
	calcSha256();
#endif

wait:
	while (1) {
		VBlankIntrWait();
	}
}
