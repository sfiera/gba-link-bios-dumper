#ifndef LINK_CABLE_MULTIBOOT_H
#define LINK_CABLE_MULTIBOOT_H

// --------------------------------------------------------------------------
// A Multiboot tool to send small programs from one GBA to up to 3 slaves.
// --------------------------------------------------------------------------
// Usage:
// - 1) Include this header in your main.cpp file and add:
//       LinkCableMultiboot* linkCableMultiboot = new LinkCableMultiboot();
// - 2) Send the ROM:
//       LinkCableMultiboot::Result result = linkCableMultiboot->sendRom(
//         romBytes, // for current ROM, use: ((const u8*)MEM_EWRAM)
//                   // ^ must be 4-byte aligned
//         romLength, // in bytes, should be multiple of 16
//         []() {
//           u16 keys = ~REG_KEYS & KEY_ANY;
//           return keys & KEY_START;
//           // (when this returns true, the transfer will be canceled)
//         }
//       );
//       // `result` should be LinkCableMultiboot::Result::SUCCESS
// - 3) (Optional) Send ROMs asynchronously:
//       LinkCableMultiboot::Async* linkCableMultibootAsync =
//         new LinkCableMultiboot::Async();
//       interrupt_init();
//       interrupt_add(INTR_VBLANK, LINK_CABLE_MULTIBOOT_ASYNC_ISR_VBLANK);
//       interrupt_add(INTR_SERIAL, LINK_CABLE_MULTIBOOT_ASYNC_ISR_SERIAL);
//       bool success = linkCableMultibootAsync->sendRom(romBytes, romLength);
//       if (success) {
//         // (monitor `playerCount()` and `getPercentage()`)
//         if (!linkCableMultibootAsync->isSending()) {
//           auto result = linkCableMultibootAsync->getResult();
//           // `result` should be
//           // LinkCableMultiboot::Async::GeneralResult::SUCCESS
//         }
//       }
// --------------------------------------------------------------------------
// considerations:
// - stop DMA before sending the ROM! (you might need to stop your audio player)
// - this restriction only applies to the sync version!
// --------------------------------------------------------------------------

#ifndef LINK_DEVELOPMENT
#pragma GCC system_header
#endif

#include "LinkRawCable.hpp"

#ifndef LINK_CABLE_MULTIBOOT_PALETTE_DATA
/**
 * @brief Palette data (controls how the logo is displayed).
 * Format: 0b1CCCDSS1, where C=color, D=direction, S=speed.
 * Default: 0b10010011
 */
#define LINK_CABLE_MULTIBOOT_PALETTE_DATA 0b10010011
#endif

#ifndef LINK_CABLE_MULTIBOOT_ASYNC_DISABLE_NESTED_IRQ
/**
 * @brief Disable nested IRQs (uncomment to enable).
 * In the async version, SERIAL IRQs can be interrupted (once they clear their
 * time-critical needs) by default, which helps prevent issues with audio
 * engines. However, if something goes wrong, you can disable this behavior.
 */
// #define LINK_CABLE_MULTIBOOT_ASYNC_DISABLE_NESTED_IRQ
#endif

LINK_VERSION_TAG LINK_CABLE_MULTIBOOT_VERSION = "vLinkCableMultiboot/v8.0.3";

#define LINK_CABLE_MULTIBOOT_TRY(CALL)                  \
  partialResult = CALL;                                 \
  if (partialResult == PartialResult::ABORTED)          \
    return error(Result::CANCELED);                     \
  else if (partialResult == PartialResult::NEEDS_RETRY) \
    goto retry;

/**
 * @brief A Multiboot tool to send small programs from one GBA to up to 3
 * slaves.
 */
class LinkCableMultiboot {
 private:
  using u32 = Link::u32;
  using u16 = Link::u16;
  using u8 = Link::u8;
  using vu32 = Link::vu32;
  using vu8 = Link::vu8;

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
  static constexpr auto MAX_BAUD_RATE = LinkRawCable::BaudRate::BAUD_RATE_3;

  struct Response {
    u32 data[LINK_RAW_CABLE_MAX_PLAYERS];
    int playerId = -1;  // (-1 = unknown)
  };

 public:
  enum class Result {
    SUCCESS,
    UNALIGNED,
    INVALID_SIZE,
    CANCELED,
    FAILURE_DURING_TRANSFER
  };

  enum class TransferMode {
    SPI = 0,
    MULTI_PLAY = 1
  };  // (used in SWI call, do not swap)

  /**
   * @brief Sends the `rom`. Once completed, the return value should be
   * `LinkCableMultiboot::Result::SUCCESS`.
   * @param rom A pointer to ROM data. Must be 4-byte aligned.
   * @param romSize Size of the ROM in bytes. It must be a number between
   * `448` and `262144`, and a multiple of `16`.
   * @param cancel A function that will be continuously invoked. If it
   * returns `true`, the transfer will be aborted.
   * @param mode Either `TransferMode::MULTI_PLAY` for GBA cable (default
   * value) or `TransferMode::SPI` for GBC cable.
   * \warning Blocks the system until completion or cancellation.
   */
  template <typename F>
  Result sendRom(const u8* rom,
                 u32 romSize,
                 F cancel,
                 TransferMode mode = TransferMode::MULTI_PLAY) {
    LINK_READ_TAG(LINK_CABLE_MULTIBOOT_VERSION);

    if ((u32)rom % 4 != 0)
      return Result::UNALIGNED;
    if (romSize < MIN_ROM_SIZE || romSize > MAX_ROM_SIZE ||
        (romSize % 0x10) != 0)
      return Result::INVALID_SIZE;

  retry:
    stop();

    // (*) instead of 1/16s, waiting a random number of frames works better
    Link::wait(INITIAL_WAIT_MIN_LINES +
               FRAME_LINES *
                   Link::_qran_range(1, INITIAL_WAIT_MAX_RANDOM_FRAMES));

    // 1. Prepare a "Multiboot Parameter Structure" in RAM.
    PartialResult partialResult = PartialResult::NEEDS_RETRY;
    Link::_MultiBootParam multiBootParameters;
    multiBootParameters.client_data[0] = CLIENT_NO_DATA;
    multiBootParameters.client_data[1] = CLIENT_NO_DATA;
    multiBootParameters.client_data[2] = CLIENT_NO_DATA;
    multiBootParameters.palette_data = LINK_CABLE_MULTIBOOT_PALETTE_DATA;
    multiBootParameters.client_bit = 0;
    multiBootParameters.boot_srcp = (u8*)rom + HEADER_SIZE;
    multiBootParameters.boot_endp = (u8*)rom + romSize;

    LINK_CABLE_MULTIBOOT_TRY(detectClients(multiBootParameters, cancel))
    LINK_CABLE_MULTIBOOT_TRY(sendHeader(multiBootParameters, rom, cancel))
    LINK_CABLE_MULTIBOOT_TRY(sendPalette(multiBootParameters, cancel))
    LINK_CABLE_MULTIBOOT_TRY(confirmHandshakeData(multiBootParameters, cancel))

    // 9. Call SWI 0x25, with r0 set to the address of the multiboot parameter
    // structure and r1 set to the communication mode (0 for normal, 1 for
    // MultiPlay).
    int result = Link::_MultiBoot(&multiBootParameters, (int)TransferMode::MULTI_PLAY);

    stop();

    // 10. Upon return, r0 will be either 0 for success, or 1 for failure. If
    // successful, all clients have received the multiboot program successfully
    // and are now executing it - you can begin either further data transfer or
    // a multiplayer game from here.
    return result == 1 ? Result::FAILURE_DURING_TRANSFER : Result::SUCCESS;
  }

 private:
  LinkRawCable linkRawCable;

  enum class PartialResult { NEEDS_RETRY, FINISHED, ABORTED };

  template <typename F>
  PartialResult detectClients(Link::_MultiBootParam& multiBootParameters,
                              F cancel) {
    // 2. Initiate a multiplayer communication session, using either Normal mode
    // for a single client or MultiPlay mode for multiple clients.
    start();

    // 3. Send the word 0x6200 repeatedly until all detected clients respond
    // with 0x720X, where X is their client number (1-3). If they fail to do
    // this after 16 tries, delay 1/16s and go back to step 2. (*)
    bool success = false;
    for (u32 t = 0; t < DETECTION_TRIES; t++) {
      auto response = transfer(CMD_HANDSHAKE, cancel);
      if (cancel())
        return PartialResult::ABORTED;

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
      return PartialResult::NEEDS_RETRY;

    // 4. Fill in client_bit in the multiboot parameter structure (with
    // bits 1-3 set according to which clients responded). Send the word
    // 0x610Y, where Y is that same set of set bits.
    auto response =
        transfer(CMD_CONFIRM_CLIENTS | multiBootParameters.client_bit, cancel);

    // The clients should respond 0x7200.
    if (!isResponseSameAsValueWithClientBit(
            response, multiBootParameters.client_bit, ACK_HANDSHAKE))
      return PartialResult::NEEDS_RETRY;

    return PartialResult::FINISHED;
  }

  template <typename F>
  PartialResult sendHeader(Link::_MultiBootParam& multiBootParameters,
                           const u8* rom,
                           F cancel) {
    // 5. Send the cartridge header, 16 bits at a time, in little endian order.
    // After each 16-bit send, the clients will respond with 0xNN0X, where NN is
    // the number of words remaining and X is the client number. (Note that if
    // transferring in the single-client 32-bit mode, you still need to send
    // only 16 bits at a time).
    u16* headerOut = (u16*)rom;
    u32 remaining = HEADER_PARTS;
    while (remaining > 0) {
      auto response = transfer(*(headerOut++), cancel);
      if (cancel())
        return PartialResult::ABORTED;

      if (!isResponseSameAsValueWithClientBit(
              response, multiBootParameters.client_bit, remaining << 8))
        return PartialResult::NEEDS_RETRY;

      remaining--;
    }

    // 6. Send 0x6200, followed by 0x620Y again.
    // The clients should respond 0x000Y and 0x720Y.
    Response response;
    response = transfer(CMD_HANDSHAKE, cancel);
    if (cancel())
      return PartialResult::ABORTED;
    if (!isResponseSameAsValueWithClientBit(response,
                                            multiBootParameters.client_bit, 0))
      return PartialResult::NEEDS_RETRY;
    response = transfer(CMD_HANDSHAKE | multiBootParameters.client_bit, cancel);
    if (cancel())
      return PartialResult::ABORTED;
    if (!isResponseSameAsValueWithClientBit(
            response, multiBootParameters.client_bit, ACK_HANDSHAKE))
      return PartialResult::NEEDS_RETRY;

    return PartialResult::FINISHED;
  }

  template <typename F>
  PartialResult sendPalette(Link::_MultiBootParam& multiBootParameters,
                            F cancel) {
    // 7. Send 0x63PP repeatedly, where PP is the palette_data you have picked
    // earlier. Do this until the clients respond with 0x73CC, where CC is a
    // random byte. Store these bytes in client_data in the parameter structure.
    u16 data = CMD_SEND_PALETTE | LINK_CABLE_MULTIBOOT_PALETTE_DATA;

    bool success = false;
    for (u32 i = 0; i < DETECTION_TRIES; i++) {
      auto response = transfer(data, cancel);
      if (cancel())
        return PartialResult::ABORTED;

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

    if (!success)
      return PartialResult::NEEDS_RETRY;

    return PartialResult::FINISHED;
  }

  template <typename F>
  PartialResult confirmHandshakeData(Link::_MultiBootParam& multiBootParameters,
                                     F cancel) {
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
    auto response = transfer(data, cancel);
    if (cancel())
      return PartialResult::ABORTED;
    if (!isResponseSameAsValue(response, multiBootParameters.client_bit,
                               ACK_RESPONSE, ACK_RESPONSE_MASK))
      return PartialResult::NEEDS_RETRY;

    return PartialResult::FINISHED;
  }

  template <typename F>
  Response transfer(u32 data, F cancel) {
    Response response;
    auto response16bit = linkRawCable.transfer(data, cancel);
    for (u32 i = 0; i < LINK_RAW_CABLE_MAX_PLAYERS; i++)
      response.data[i] = response16bit.data[i];
    response.playerId = response16bit.playerId;
    return response;
  }

  void start() {
    linkRawCable.activate(MAX_BAUD_RATE);
  }

  void stop() {
    linkRawCable.deactivate();
  }

  Result error(Result error) {
    stop();
    return error;
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
      if (value == LINK_RAW_CABLE_DISCONNECTED) {
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

extern LinkCableMultiboot* linkCableMultiboot;

#endif  // LINK_CABLE_MULTIBOOT_H
