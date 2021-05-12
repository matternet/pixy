//
// begin license header
//
// This file is part of Pixy CMUcam5 or "Pixy" for short
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

#include "blobs.h"
#include "progblobs.h"
#include "pixy_init.h"
#include "camera.h"
#include "led.h"
#include "serial.h"
#include "exec.h"
#include "sdmmc.h"


static int blobsSetup();
static int blobsLoop();

Program g_progBlobs =
{
    "Color_connected_components",
    "perform color connected components",
    blobsSetup,
    blobsLoop
};

static bool initialized_ = false;
static bool enable_image_logging_ = false;
static Qqueue qqueue_;
static Blobs blobs_;

static uint32_t getTxData(uint8_t *data, uint32_t len)
{
    return blobs_.getBlock(data, len);
}

static void enable_logging(bool enable)
{
    static bool sd_card_header_intialized = false;

    // Only update the SD Card's header block once on first enable.
    if (!sd_card_header_intialized && enable)
    {
        sdmmc_updateHeader();
        sd_card_header_intialized = true;
    }

    enable_image_logging_ = enable;
}

static bool handleRxData(uint8_t cmd, const uint8_t *data, uint32_t dlen)
{
    switch (cmd)
    {
    case SER_CMD_START_IMAGE_LOGGING:
        enable_logging(true);
        return true;

    case SER_CMD_STOP_IMAGE_LOGGING:
        enable_logging(false);
        return true;

    default:
        break;
    }

    return false;
}

static int sendBlobs(Chirp *chirp, const BlobA *blobs, uint32_t len, uint8_t renderFlags=RENDER_FLAG_FLUSH)
{
    if (chirp == NULL || chirp->connected() == false)
        return -1;

    CRP_RETURN(chirp, HTYPE(FOURCC('C','C','B','1')), HINT8(renderFlags), HINT16(CAM_RES2_WIDTH), HINT16(CAM_RES2_HEIGHT), UINTS16(len*sizeof(BlobA)/sizeof(uint16_t), blobs), END);
    return 0;
}

static int blobsSetup()
{
    if (initialized_ == false)
    {
#ifdef ENABLE_IMAGE_LOGGING_AT_BOOT
        enable_logging(true);
#endif

        ser_init(getTxData, handleRxData);
        initialized_ = true;
    }

    // setup camera mode
    cam_setMode(CAM_MODE1);

    // setup qqueue and M0
    qqueue_.flush();
    exec_runM0(0);

    // flush serial receive queue
    ser_flush();
    return 0;
}

static int blobsLoop()
{
    BlobA *blobs;
    uint32_t numBlobs;

    // create blobs
    if (blobs_.blobify(&qqueue_) < 0)
    {
        return 0;
    }

    // send blobs over USB if available
    blobs_.getBlobs(&blobs, &numBlobs);
    sendBlobs(g_chirpUsb, blobs, numBlobs);

    // Write frame buffer to SD Card if available
    if (enable_image_logging_ && blobs_.frameBufValid())
    {
        led_setRGB(0, 50, 0);
        sdmmc_writeFrame((void*)MEM_SD_FRAME_LOC, CAM_RES2_WIDTH * CAM_RES2_HEIGHT, blobs, numBlobs);
        led_setRGB(0, 0, 0);
    }

    // can do work here while waiting for more data in queue
    while(!qqueue_.queued())
    {
        ser_processInput();
    }

    return 0;
}
