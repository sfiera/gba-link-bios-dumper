#include "link.h"

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
