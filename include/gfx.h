#ifndef _GFX_H
#define _GFX_H

#include "types.h"

#define REG_DISPCNT (*(volatile u16*)0x4000000)

#define REG_BG0CNT	(*(volatile u16*)0x4000008)
#define REG_BG0HOFS	(*(volatile u16*)0x4000010)
#define REG_BG0VOFS	(*(volatile u16*)0x4000012)

extern const u32 gfx_failure[7];
extern const u32 gfx_success[7];
extern const u32 gfx_upload[7];
extern const u32 gfx_download[7];

void gfx_init();
void gfx_show(const u32* icon);

#endif  // _GFX_H
