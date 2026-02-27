#include "link.h"

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

static bool send_header(u8 client_mask, const u8* rom);
static bool send_palette(u8 client_mask, u8* client_data);
static bool confirm_handshake_data(u8 client_mask, const u8* client_data, u8* handshake_data);

static u32 randomSeed = 123;

static int _qran() {
    randomSeed = 1664525 * randomSeed + 1013904223;
    return (randomSeed >> 16) & 0x7FFF;
}

static int _qran_range(int min, int max) {
    return (_qran() * (max - min) >> 15) + min;
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

#define LINK_CABLE_MULTIBOOT_PALETTE_DATA 0b10010011

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

static u32 _MultiBoot(const _MultiBootParam* param, u32 mbmode) {
    register union {
        const _MultiBootParam* ptr;
        u32 res;
    } r0 asm("r0") = {param};
    register u32 r1 asm("r1") = mbmode;
    asm volatile inline("swi 0x25 << ((1f - . == 4) * -16); 1:"
                        : "+r"(r0), "+r"(r1)::"r3");
    return r0.res;
}

void link_start() {
  REG_RCNT         = 0x0000;
  REG_SIOMLT_SEND  = 0x0000;
  REG_SIOCNT       = 0x2003;
}

void link_stop() {
  REG_SIOMLT_SEND  = 0x0000;
  REG_RCNT         = 0x0000;
}

bool link_send(u16 message) {
    REG_SIOMLT_SEND = message;
    REG_SIOCNT |= 0x0080;
    while (REG_SIOCNT & 0x0080) {}
    return (REG_SIOCNT & 0x0048) == 0x0008;
}

bool link_multiboot_send(const u8* rom, u32 romSize) {
    while (true) {
        link_stop();

        // (*) instead of 1/16s, waiting a random number of frames works better
        wait(INITIAL_WAIT_MIN_LINES +
             FRAME_LINES *
             _qran_range(1, INITIAL_WAIT_MAX_RANDOM_FRAMES));

        // 1. Prepare a "Multiboot Parameter Structure" in RAM.
        _MultiBootParam param;
        param.client_data[0] = CLIENT_NO_DATA;
        param.client_data[1] = CLIENT_NO_DATA;
        param.client_data[2] = CLIENT_NO_DATA;
        param.palette_data = LINK_CABLE_MULTIBOOT_PALETTE_DATA;
        param.client_bit = 0;
        param.boot_srcp = (u8*)rom + HEADER_SIZE;
        param.boot_endp = (u8*)rom + romSize;

        // 2. Initiate a multiplayer communication session, using either Normal mode
        // for a single client or MultiPlay mode for multiple clients.
        link_start();

        if (link_detect_clients(CMD_HANDSHAKE, ACK_HANDSHAKE, &param.client_bit) &&
            link_confirm_clients(CMD_CONFIRM_CLIENTS, ACK_HANDSHAKE, param.client_bit) &&
            send_header(param.client_bit, rom) &&
            send_palette(param.client_bit, param.client_data) &&
            confirm_handshake_data(param.client_bit, param.client_data, &param.handshake_data)) {
            int result = _MultiBoot(&param, 1);
            link_stop();
            return result == 0;
        }
    }
}

typedef struct {
    u32 data[4];
    int playerId;  // (-1 = unknown)
} Response;

Response transfer(u16 data) {
    Response response = {{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, -1};
    if (!link_send(data)) {
        return response;
    }
    for (u32 i = 0; i < 4; i++) {
        response.data[i] = REG_SIOMULTI[i];
    }
    response.playerId = (REG_SIOCNT & (0b11 << 4)) >> 4;

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

bool link_detect_clients(u16 cmd, u16 ack, u8* client_mask) {
    // 3. Send the word 0x6200 repeatedly until all detected clients respond
    // with 0x720X, where X is their client number (1-3). If they fail to do
    // this after 16 tries, delay 1/16s and go back to step 2. (*)
    *client_mask = 0;
    for (u32 t = 0; t < DETECTION_TRIES; t++) {
        Response response = transfer(cmd);

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            u32 value = response.data[1 + i];
            if (value == 0xFFFF) {
                continue;
            }
            u8 clientId = 1 << (i + 1);
            if (value != (ack | clientId)) {
                *client_mask = 0;
                break;
            }
            *client_mask |= clientId;
        }

        if (*client_mask) {
            return true;
        }
    }

    return false;
}

bool link_confirm_clients(u16 cmd, u16 ack, u8 client_mask) {
    // 4. Fill in client_bit in the multiboot parameter structure (with
    // bits 1-3 set according to which clients responded). Send the word
    // 0x610Y, where Y is that same set of set bits.
    Response response = transfer(cmd | client_mask);

    // The clients should respond 0x7200.
    return isResponseSameAsValueWithClientBit(response, client_mask, ack);
}

static bool send_header(u8 client_mask, const u8* rom) {
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
                        response, client_mask, remaining << 8))
            return false;

        remaining--;
    }

    // 6. Send 0x6200, followed by 0x620Y again.
    // The clients should respond 0x000Y and 0x720Y.
    Response response;
    response = transfer(CMD_HANDSHAKE);
    if (!isResponseSameAsValueWithClientBit(response,
                                            client_mask, 0))
        return false;
    response = transfer(CMD_HANDSHAKE | client_mask);
    if (!isResponseSameAsValueWithClientBit(
                    response, client_mask, ACK_HANDSHAKE))
        return false;

    return true;
}

static bool send_palette(u8 client_mask, u8* client_data) {
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
            if (!(client_mask & clientBit) ||
                ((value & ACK_RESPONSE_MASK) != ACK_RESPONSE)) {
                successes = 0;
                break;
            }
            client_data[i] = value & 0xFF;
            successes |= clientBit;
        }

        if (successes == client_mask) {
            return true;
        }
    }

    return false;
}

static bool confirm_handshake_data(u8 client_mask, const u8* client_data, u8* handshake_data) {
    // 8. Calculate the handshake_data byte and store it in the parameter
    // structure. This should be calculated as 0x11 + the sum of the three
    // client_data bytes. Send 0x64HH, where HH is the handshake_data.
    // The clients should respond 0x77GG, where GG is something unimportant.
    *handshake_data = (HANDSHAKE_DATA + client_data[0] + client_data[1] + client_data[2]) & 0xFF;
    u16 msg = CMD_CONFIRM_HANDSHAKE_DATA | *handshake_data;
    Response response = transfer(msg);
    return isResponseSameAsValue(response, client_mask, ACK_RESPONSE, ACK_RESPONSE_MASK);
}
