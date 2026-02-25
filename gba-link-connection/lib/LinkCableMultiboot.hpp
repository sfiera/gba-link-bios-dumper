#ifndef LINK_CABLE_MULTIBOOT_H
#define LINK_CABLE_MULTIBOOT_H

#include <gba_base.h>

#define LINK_CABLE_MULTIBOOT_PALETTE_DATA 0b10010011

inline vu16& _REG_RCNT = *reinterpret_cast<vu16*>(0x04000000 + 0x0134);
inline vu16& _REG_SIOCNT = *reinterpret_cast<vu16*>(0x04000000 + 0x0128);
inline vu16& _REG_SIOMLT_SEND = *reinterpret_cast<vu16*>(0x04000000 + 0x012A);
inline vu16* const _REG_SIOMULTI = reinterpret_cast<vu16*>(0x04000000 + 0x0120);
inline vu16& _REG_VCOUNT = *reinterpret_cast<vu16*>(0x04000000 + 0x0006);

inline u32 randomSeed = 123;

static inline int _qran() {
  randomSeed = 1664525 * randomSeed + 1013904223;
  return (randomSeed >> 16) & 0x7FFF;
}

static inline int _qran_range(int min, int max) {
  return (_qran() * (max - min) >> 15) + min;
}

static inline void wait(u32 verticalLines) {
  u32 count = 0;
  u32 vCount = _REG_VCOUNT;

  while (count < verticalLines) {
    if (_REG_VCOUNT != vCount) {
      count++;
      vCount = _REG_VCOUNT;
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
static LINK_INLINE auto _MultiBoot(const _MultiBootParam* param,
                                   u32 mbmode) noexcept {
  register union {
    const _MultiBootParam* ptr;
    int res;
  } r0 asm("r0") = {param};
  register auto r1 asm("r1") = mbmode;
  asm volatile inline("swi 0x25 << ((1f - . == 4) * -16); 1:"
                      : "+r"(r0), "+r"(r1)::"r3");
  return r0.res;
}

class LinkCableMultiboot {
 private:
  static constexpr int MIN_ROM_SIZE = 0x100 + 0xC0;
  static constexpr int MAX_ROM_SIZE = 256 * 1024;
  static constexpr int FRAME_LINES = 228;
  static constexpr int INITIAL_WAIT_MIN_FRAMES = 4;
  static constexpr int INITIAL_WAIT_MAX_RANDOM_FRAMES = 10;
  static constexpr int INITIAL_WAIT_MIN_LINES =
      FRAME_LINES * INITIAL_WAIT_MIN_FRAMES;
  static constexpr int DETECTION_TRIES = 16;
  static constexpr int MAX_CLIENTS = 3;
  static constexpr int CLIENT_NO_DATA = 0xFF;
  static constexpr int CMD_HANDSHAKE = 0x6200;
  static constexpr int ACK_HANDSHAKE = 0x7200;
  static constexpr int CMD_CONFIRM_CLIENTS = 0x6100;
  static constexpr int CMD_SEND_PALETTE = 0x6300;
  static constexpr int HANDSHAKE_DATA = 0x11;
  static constexpr int CMD_CONFIRM_HANDSHAKE_DATA = 0x6400;
  static constexpr int ACK_RESPONSE = 0x7300;
  static constexpr int ACK_RESPONSE_MASK = 0xFF00;
  static constexpr int HEADER_SIZE = 0xC0;
  static constexpr int HEADER_PARTS = HEADER_SIZE / 2;

 public:
  static bool sendRom(const u8* rom, u32 romSize) {
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

      if (detectClients(multiBootParameters) &&
          sendHeader(multiBootParameters, rom) &&
          sendPalette(multiBootParameters) &&
          confirmHandshakeData(multiBootParameters)) {
        int result = _MultiBoot(&multiBootParameters, 1);
        stop();
        return result == 0;
      }
    }
  }

 private:
  struct Response {
    u32 data[4];
    int playerId = -1;  // (-1 = unknown)
  };

  static Response transfer(u16 data) {
    _REG_SIOMLT_SEND = data;
    startTransfer();
    while (isSending()) {}
    if (!allReady() || hasError())
      return Response{{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, -1};

    Response response;
    for (u32 i = 0; i < 4; i++)
      response.data[i] = _REG_SIOMULTI[i];
    response.playerId = (_REG_SIOCNT & (0b11 << 4)) >> 4;

    return response;
  }

  static void startTransfer() { setBitHigh(7); }
  [[nodiscard]] static bool allReady() { return isBitHigh(3); }
  [[nodiscard]] static bool hasError() { return isBitHigh(6); }
  [[nodiscard]] static bool isSending() { return isBitHigh(7); }
  static bool isBitHigh(u8 bit) { return (_REG_SIOCNT >> bit) & 1; }
  static void setBitHigh(u8 bit) { _REG_SIOCNT |= 1 << bit; }

  static bool detectClients(_MultiBootParam& multiBootParameters) {
    // 2. Initiate a multiplayer communication session, using either Normal mode
    // for a single client or MultiPlay mode for multiple clients.
    start();

    // 3. Send the word 0x6200 repeatedly until all detected clients respond
    // with 0x720X, where X is their client number (1-3). If they fail to do
    // this after 16 tries, delay 1/16s and go back to step 2. (*)
    bool success = false;
    for (u32 t = 0; t < DETECTION_TRIES; t++) {
      auto response = transfer(CMD_HANDSHAKE);
      multiBootParameters.client_bit = 0;

      success =
          validateResponse(response, [&multiBootParameters](u32 i, u16 value) {
            if ((value & 0xFFF0) == ACK_HANDSHAKE) {
              u8 clientId = value & 0xF;
              u8 expectedClientId = 1 << (i + 1);
              if (clientId == expectedClientId) {
                multiBootParameters.client_bit |= clientId;
                return true;
              }
            }
            return false;
          });

      if (success)
        break;
    }

    if (!success)
      return false;

    // 4. Fill in client_bit in the multiboot parameter structure (with
    // bits 1-3 set according to which clients responded). Send the word
    // 0x610Y, where Y is that same set of set bits.
    auto response =
        transfer(CMD_CONFIRM_CLIENTS | multiBootParameters.client_bit);

    // The clients should respond 0x7200.
    if (!isResponseSameAsValueWithClientBit(
            response, multiBootParameters.client_bit, ACK_HANDSHAKE))
      return false;

    return true;
  }

  static bool sendHeader(_MultiBootParam& multiBootParameters, const u8* rom) {
    // 5. Send the cartridge header, 16 bits at a time, in little endian order.
    // After each 16-bit send, the clients will respond with 0xNN0X, where NN is
    // the number of words remaining and X is the client number. (Note that if
    // transferring in the single-client 32-bit mode, you still need to send
    // only 16 bits at a time).
    u16* headerOut = (u16*)rom;
    u32 remaining = HEADER_PARTS;
    while (remaining > 0) {
      auto response = transfer(*(headerOut++));
      if (!isResponseSameAsValueWithClientBit(
              response, multiBootParameters.client_bit, remaining << 8))
        return false;

      remaining--;
    }

    // 6. Send 0x6200, followed by 0x620Y again.
    // The clients should respond 0x000Y and 0x720Y.
    Response response;
    response = transfer(CMD_HANDSHAKE);
    if (!isResponseSameAsValueWithClientBit(response,
                                            multiBootParameters.client_bit, 0))
      return false;
    response = transfer(CMD_HANDSHAKE | multiBootParameters.client_bit);
    if (!isResponseSameAsValueWithClientBit(
            response, multiBootParameters.client_bit, ACK_HANDSHAKE))
      return false;

    return true;
  }

  static bool sendPalette(_MultiBootParam& multiBootParameters) {
    // 7. Send 0x63PP repeatedly, where PP is the palette_data you have picked
    // earlier. Do this until the clients respond with 0x73CC, where CC is a
    // random byte. Store these bytes in client_data in the parameter structure.
    u16 data = CMD_SEND_PALETTE | LINK_CABLE_MULTIBOOT_PALETTE_DATA;

    bool success = false;
    for (u32 i = 0; i < DETECTION_TRIES; i++) {
      auto response = transfer(data);
      u8 sendMask = multiBootParameters.client_bit;
      success = validateResponse(
                    response,
                    [&multiBootParameters, &sendMask](u32 i, u16 value) {
                      u8 clientBit = 1 << (i + 1);
                      if ((multiBootParameters.client_bit & clientBit) &&
                          ((value & ACK_RESPONSE_MASK) == ACK_RESPONSE)) {
                        multiBootParameters.client_data[i] = value & 0xFF;
                        sendMask &= ~clientBit;
                        return true;
                      }
                      return false;
                    }) &&
                sendMask == 0;

      if (success)
        break;
    }

    return success;
  }

  static bool confirmHandshakeData(_MultiBootParam& multiBootParameters) {
    // 8. Calculate the handshake_data byte and store it in the parameter
    // structure. This should be calculated as 0x11 + the sum of the three
    // client_data bytes. Send 0x64HH, where HH is the handshake_data.
    // The clients should respond 0x77GG, where GG is something unimportant.
    multiBootParameters.handshake_data =
        (HANDSHAKE_DATA + multiBootParameters.client_data[0] +
         multiBootParameters.client_data[1] +
         multiBootParameters.client_data[2]) %
        256;

    u16 data = CMD_CONFIRM_HANDSHAKE_DATA | multiBootParameters.handshake_data;
    auto response = transfer(data);
    return isResponseSameAsValue(response, multiBootParameters.client_bit,
                                 ACK_RESPONSE, ACK_RESPONSE_MASK);
  }

  static void start() {
    _REG_RCNT         = 0x0000;
    _REG_SIOMLT_SEND  = 0x0000;
    _REG_SIOCNT       = 0x2003;
  }

  static void stop() {
    _REG_SIOMLT_SEND = 0;
    _REG_RCNT = (_REG_RCNT & ~(1 << 14)) | (1 << 15);
  }

  static bool isResponseSameAsValue(Response response,
                                    u8 clientMask,
                                    u16 wantedValue,
                                    u16 mask = 0xFFFF) {
    return validateResponse(
        response, [&clientMask, &wantedValue, &mask](u32 i, u32 value) {
          u8 clientBit = 1 << (i + 1);
          bool isInvalid =
              (clientMask & clientBit) && ((value & mask) != wantedValue);
          return !isInvalid;
        });
  }

  static bool isResponseSameAsValueWithClientBit(Response response,
                                                 u8 clientMask,
                                                 u32 wantedValue) {
    return validateResponse(
        response, [&clientMask, &wantedValue](u32 i, u32 value) {
          u8 clientBit = 1 << (i + 1);
          bool isInvalid =
              (clientMask & clientBit) && (value != (wantedValue | clientBit));
          return !isInvalid;
        });
  }

  template <typename F>
  static bool validateResponse(Response response, F check) {
    u32 count = 0;
    for (u32 i = 0; i < MAX_CLIENTS; i++) {
      u32 value = response.data[1 + i];
      if (value == 0xFFFF) {
        // Note that throughout this process, any clients that are not
        // connected will always respond with 0xFFFF - be sure to ignore them.
        continue;
      }

      if (!check(i, value))
        return false;
      count++;
    }

    return count > 0;
  }
};

#endif  // LINK_CABLE_MULTIBOOT_H
