#ifndef _LINK_H
#define _LINK_H

#include <gba_base.h>

#define REG_RCNT         (*(vu16*)0x04000134)
#define REG_SIOCNT       (*(vu16*)0x04000128)
#define REG_SIOMLT_SEND  (*(vu16*)0x0400012A)
#define REG_SIOMULTI     ((vu16*)0x04000120)
#define REG_VCOUNT       (*(vu16*)0x04000006)

void link_start();
void link_stop();
bool link_send(u16 message);

#endif // _LINK_H
