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

#include "cameravals.h"
#include "chirp.hpp"
#include "debug.h"
#include "lpc43xx_sdmmc.h"
#include "lpc43xx_scu.h"
#include "lpc43xx_cgu.h"
#include "lpc43xx_timer.h"
#include "lpc_types.h"
#include "misc.h"
#include "pixy_init.h"
#include "pixyvals.h"
#include "sdmmc.h"

#include <string.h>

#define LOG_PREFIX              "SDMMC: "
#define HEADER_BLOCK_ID_A       0
#define HEADER_BLOCK_ID_B       1
#define SESSION_BLOCK_START     2
#define FRAME_HEADER_BLOCK_SIZE 1
#define FRAME_BYTES             (CAM_RES2_WIDTH * CAM_RES2_HEIGHT)
#define BLOCKS_PER_FRAME        (FRAME_BYTES / MMC_SECTOR_SIZE + FRAME_HEADER_BLOCK_SIZE)
#define FRAMES_PER_SESSION      6000
#define MAX_SESSIONS            80
#define DEV_BLOCKS_REQUIRED     (SESSION_BLOCK_START + (BLOCKS_PER_FRAME * FRAMES_PER_SESSION * MAX_SESSIONS))

static int read_blocks(const uint32_t &blkStart, const uint32_t &blkCnt, Chirp *chirp);

// Expose reading of blocks from SD Card
static const ProcModule g_module[] =
{
    {
    "read_blocks",
    (ProcPtr)read_blocks,
    {CRP_UINT32, CRP_UINT32, END},
    "Read blocks from SD Card"
    "@p block_start"
    "@p block_count"
    "@r 0 if success, negative if error"
    },
    END
};

static bool init_success_ = false;
static mci_card_struct sdcardinfo_;
static volatile int32_t sdio_wait_exit_ = 0;
static uint32_t session_cnt_ = 0;
static int32_t session_id_ = -1;
static uint32_t session_block_ = SESSION_BLOCK_START;
static uint32_t frame_index_ = 0;


// Function used by SDMMC stack for delaying time
static void sdmmc_msdelay(uint32_t time)
{
    delayms(time);
}

// Function used by SDMMC stack
static void sdmmc_setup_wakeup(void *bits)
{
    if (bits == NULL)
        return;

    uint32_t bit_mask = *((uint32_t *)bits);
    NVIC_ClearPendingIRQ(SDIO_IRQn);
    sdio_wait_exit_ = 0;
    Chip_SDIF_SetIntMask(LPC_SDMMC, bit_mask);
    NVIC_EnableIRQ(SDIO_IRQn);
}

// Function used by SDMMC stack
static uint32_t sdmmc_irq_driven_wait(void)
{
    // Wait for event
    while (sdio_wait_exit_ == 0) {}

    // Get status and clear interrupts
    uint32_t status = Chip_SDIF_GetIntStatus(LPC_SDMMC);
    Chip_SDIF_ClrIntStatus(LPC_SDMMC, status);
    Chip_SDIF_SetIntMask(LPC_SDMMC, 0);
    return status;
}

// Function used by SDMMC stack
extern "C" void SDIO_IRQHandler(void)
{
    NVIC_DisableIRQ(SDIO_IRQn);
    sdio_wait_exit_ = 1;
}

// Read blocks from SD Card and send over USB
static int read_blocks(const uint32_t &blkStart, const uint32_t &blkCnt, Chirp *chirp)
{
    const int32_t bytecnt = blkCnt * MMC_SECTOR_SIZE;
    uint8_t *buffer = (uint8_t*)MEM_USB_FRAME_LOC;
    int32_t bytes_read;
    int32_t len;

    if (blkCnt == 0 || blkCnt > BLOCKS_PER_FRAME || chirp == NULL)
        return -1;

    // fill buffer contents manually for return data
    len = Chirp::serialize(chirp, buffer, MEM_USB_FRAME_SIZE, UINTS8_NO_COPY(bytecnt), END);
    if (len <= 0)
        return -1;

    // write frame after chirp args
    bytes_read = Chip_SDMMC_ReadBlocks(LPC_SDMMC, buffer+len, blkStart, blkCnt);
    if (bytes_read != bytecnt)
        return -1;

    // tell chirp to use this buffer
    chirp->useBuffer(buffer, len + bytecnt);
    return bytecnt;
}

// Read a header block from SD Card
static bool read_header(SdmmcHeader *header, int block_num)
{
    if (header == NULL)
        return false;

    uint8_t buffer[MMC_SECTOR_SIZE];
    int32_t bytes_read = Chip_SDMMC_ReadBlocks(LPC_SDMMC, buffer, block_num, 1);
    if (bytes_read == MMC_SECTOR_SIZE)
    {
        memcpy(header, buffer, sizeof(SdmmcHeader));
        return true;
    }
    return false;
}

// Write a header block to SD Card
static bool write_header(uint32_t block_id, uint32_t session_cnt)
{
    uint8_t buffer[MMC_SECTOR_SIZE];
    SdmmcHeader *header = (SdmmcHeader *)buffer;

    memset(buffer, 0, sizeof(buffer));
    memcpy(&header->magic, SDMMC_HEADER_MAGIC, sizeof(SDMMC_HEADER_MAGIC));
    header->version = SDMMC_HEADER_VERSION;
    header->session_cnt = session_cnt;
    header->crc8 = crc8(header, offsetof(SdmmcHeader, crc8));

    int32_t bytes_written = Chip_SDMMC_WriteBlocks(LPC_SDMMC, buffer, block_id, 1);
    return bytes_written == MMC_SECTOR_SIZE;
}

// Verify header block
static bool verify_header(const SdmmcHeader *header)
{
    if (header == NULL)
        return false;

    return (memcmp(&header->magic, SDMMC_HEADER_MAGIC, sizeof(header->magic)) == 0) &&
           (header->crc8 == crc8(header, offsetof(SdmmcHeader, crc8)));
}

// Verify the header blocks and formats the header if invalid
static bool init_card(uint32_t &session_cnt)
{
    SdmmcHeader headerA;
    SdmmcHeader headerB;
    SdmmcHeader *header = NULL;

    bool validA = read_header(&headerA, 0) && verify_header(&headerA);
    bool validB = read_header(&headerB, 1) && verify_header(&headerB);

    if (validA && validB)
    {
        // Both header blocks are valid. Use the session count that is greater.
        header = (headerA.session_cnt > headerB.session_cnt) ? &headerA : &headerB;
    }
    else if (validA)
    {
        // Header B is corrupted. Use Header A
        header = &headerA;
    }
    else if (validB)
    {
        // Header A is corrupted. Use Header B
        header = &headerB;
    }
    else
    {
        // Both header blocks are invalid. Must be a new card. Reformat...
        printf(LOG_PREFIX "Both headers not valid. Formatting SD Card.\n");
        session_cnt = 0;
        return sdmmc_format();
    }

    if (header != NULL)
    {
        // Increment the session count and write it back to the oldest header block
        header->session_cnt++;

        if (header == &headerA)
        {
            if (write_header(HEADER_BLOCK_ID_B, header->session_cnt) == false)
            {
                printf(LOG_PREFIX "Failed to update HeaderB\n");
                return false;
            }
        }
        else if (write_header(HEADER_BLOCK_ID_A, header->session_cnt) == false)
        {
            printf(LOG_PREFIX "Failed to update HeaderA\n");
            return false;
        }
    }

    session_cnt = header->session_cnt;
    return true;
}

// Initialize SD Card
bool sdmmc_init(void)
{
    init_success_ = false;

    // Register USB functions
    g_chirpUsb->registerModule(g_module);

    memset(&sdcardinfo_, 0, sizeof(sdcardinfo_));
    sdcardinfo_.card_info.evsetup_cb = sdmmc_setup_wakeup;
    sdcardinfo_.card_info.waitfunc_cb = sdmmc_irq_driven_wait;
    sdcardinfo_.card_info.msdelay_func = sdmmc_msdelay;

    // Initialize SDIO peripheral and clock
    CGU_EntityConnect(CGU_CLKSRC_PLL1, CGU_BASE_SDIO);
    Chip_SDIF_Init(LPC_SDMMC);
    NVIC_EnableIRQ(SDIO_IRQn);

    // Attempt to acquire the card
    if (Chip_SDMMC_Acquire(LPC_SDMMC, &sdcardinfo_) == false)
    {
        printf(LOG_PREFIX "Failed to acquire card\n");
        return false;
    }

    uint64_t dev_size = Chip_SDMMC_GetDeviceSize(LPC_SDMMC);
    int32_t dev_blocks = Chip_SDMMC_GetDeviceBlocks(LPC_SDMMC);
    printf(LOG_PREFIX "Device Size: %llu\n", dev_size);
    printf(LOG_PREFIX "Device Blocks: %u\n", dev_blocks);

    if (dev_blocks < DEV_BLOCKS_REQUIRED)
    {
        printf(LOG_PREFIX "Error: SD Card too small. Required blocks: %u\n", DEV_BLOCKS_REQUIRED);
        return false;
    }

    init_success_ = true;
    return true;
}

bool sdmmc_updateHeader()
{
    if (init_success_ == false)
        return false;

    session_cnt_ = 0;
    if (!init_card(session_cnt_))
        return false;

    // Calculate the session block for use with storing images
    session_id_ = session_cnt_ % MAX_SESSIONS;
    session_block_ = SESSION_BLOCK_START + (session_id_ * BLOCKS_PER_FRAME * FRAMES_PER_SESSION);

    // Clear the first block of the session's first frame
    uint8_t buffer[MMC_SECTOR_SIZE];
    memset(&buffer, 0xff, sizeof(buffer));
    Chip_SDMMC_WriteBlocks(LPC_SDMMC, buffer, session_block_, 1);

    printf(LOG_PREFIX "Session Count: %u\n", session_cnt_);
    printf(LOG_PREFIX "Session Index: %u\n", session_id_);
    printf(LOG_PREFIX "Session Block: %u\n", session_block_);
    return true;
}

// Write the header blocks to default values
bool sdmmc_format()
{
    if (init_success_ == false)
        return false;

    return write_header(HEADER_BLOCK_ID_A, 0) && write_header(HEADER_BLOCK_ID_B, 0);
}

// Write a frame to the SD Card
bool sdmmc_writeFrame(void *frame, uint32_t len, const BlobA *blobs, uint16_t blob_cnt)
{
    static uint32_t s_frame_cnt = 0;
    static uint32_t s_last_write_time_us = 0;

    if (init_success_ == false || frame == NULL || session_id_ < 0)
        return false;

    if (blob_cnt > MAX_BLOBS)
        blob_cnt = MAX_BLOBS;

    // Get current monotonic time since bootup (microseconds)
    uint32_t starttime_us;
    setTimer(&starttime_us);

    // Caculate block to write to.
    int block = session_block_ + (frame_index_ * BLOCKS_PER_FRAME);
    int numblocks = len / MMC_SECTOR_SIZE + FRAME_HEADER_BLOCK_SIZE;

    // Prepare frame header
    SdmmcFrameHeader *header = (SdmmcFrameHeader*)frame;
    header->session_cnt = session_cnt_;
    header->frame_cnt = s_frame_cnt;
    header->timestamp_us = starttime_us;
    header->last_write_time_us = s_last_write_time_us;
    header->blob_cnt = blob_cnt;
    memcpy(header->blobs, blobs, sizeof(BlobA) * blob_cnt);
    header->crc8 = crc8(header, offsetof(SdmmcFrameHeader, crc8));

    int32_t ret = Chip_SDMMC_WriteBlocks(LPC_SDMMC, frame, block, numblocks);

    // Calculate elapsed time
    uint32_t endtime_us;
    setTimer(&endtime_us);
    s_last_write_time_us = endtime_us - starttime_us;

    frame_index_++;
    if (frame_index_ >= FRAMES_PER_SESSION)
        frame_index_ = 0;

    s_frame_cnt++;
    return ret == (int32_t)len;
}
