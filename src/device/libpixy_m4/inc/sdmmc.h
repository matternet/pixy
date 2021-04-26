//
// begin license header
//
// Copyright 2021 Matternet
//
// All Pixy source code is provided under the terms of the
// GNU General Public License v2 (http://www.gnu.org/licenses/gpl-2.0.html).
// Those wishing to use Pixy source code, software and/or
// technologies under different licensing terms should contact us at
// cmucam@cs.cmu.edu. Such licensing terms are available for
// all portions of the Pixy codebase presented here.
//
// end license header
//

#ifndef __SDMMC_H__
#define __SDMMC_H__

#include <stdint.h>

#define SDMMC_HEADER_MAGIC    "MTTR"
#define SDMMC_HEADER_VERSION  1

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint32_t version;
    uint32_t boot_cnt;
    uint16_t crc16;
} SdmmcHeader;


bool sdmmc_init(void);
bool sdmmc_format();
bool sdmmc_writeFrame(void *frame, uint32_t len);

#endif
