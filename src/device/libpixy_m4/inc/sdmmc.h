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

#include "blobs.h"
#include "pixytypes.h"

#define SDMMC_HEADER_MAGIC    "MTTR"
#define SDMMC_HEADER_VERSION  1

typedef struct __attribute__((packed))
{
    uint32_t magic;               // A specific number to make sure the sd card is formatted correctly
    uint32_t version;             // Version of header structure
    uint32_t session_cnt;         // Current recording session
    uint8_t  crc8;                // Cyclic Redundancy Check
} SdmmcHeader;

typedef struct __attribute__((packed))
{
    uint32_t session_cnt;         // Reference to session counter
    uint32_t frame_cnt;           // Frame counter
    uint32_t timestamp_us;        // Monotonic timestamp (microseconds since boot)
    uint32_t last_write_time_us;  // Elapsed time of last SD Card write
    uint16_t blob_cnt;            // Number of detected blobs
    BlobA    blobs[MAX_BLOBS];    // Detected blob information
    uint8_t  crc8;                // Cyclic Redundancy Check
} SdmmcFrameHeader;


bool sdmmc_init(void);
bool sdmmc_format();
bool sdmmc_updateHeader();
bool sdmmc_writeFrame(void *frame, uint32_t len, const BlobA *blobs, uint16_t blob_cnt);

#endif
