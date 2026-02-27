#ifndef _CRC32_H
#define _CRC32_H

#include "types.h"

void crc32_init();
u32 crc32(u32 init, const u8* data, u32 size);

#endif // _CRC32_H
