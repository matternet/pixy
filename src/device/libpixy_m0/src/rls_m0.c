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

#include "rls_m0.h"
#include "frame_m0.h"
#include "chirp.h"
#include "qqueue.h"
#include "pixyvals.h"
#include "assembly.h"

#define CAMERA_FPS     50    // Frame rate of camera
#define IMAGE_LOG_FPS  10    // Frame rate of logging images to SD Card

static const uint32_t DIVIDER_FPS = CAMERA_FPS / IMAGE_LOG_FPS;
static const uint32_t MAX_NEW_QVALS_PER_LINE  = ((CAM_RES2_WIDTH/3)+2);
static const uint32_t PIXEL_THRESHOLD = 170;
static const uint32_t WIDTH = CAM_RES2_WIDTH;
static const uint32_t INVALID_COL = CAM_RES2_WIDTH + 1;


_ASM_FUNC uint32_t processLine(uint32_t *gpio, uint8_t *framebuf, Qval *qMem, uint32_t writeFrame)
{
// r0: gpio register
// r1: frame buffer
// r2: Q memory
// r3: col_current
// r4: col_start
// r5: Q count
// r6: scratch
// r7: scratch; pixel value
// r8: invalid column
// r9: writeFrame
// r10: max Q values per line
// r11: pixel threshold
// r12: width

// Note: r8 - r12 are limited access registers.

    _ASM_START
    _ASM_IMPORT(callSyncM1)

#ifdef KEIL
    _ASM(PUSH   {r1-r7, lr})
#else
    _ASM(PUSH   {r1-r7})
#endif

    // Save the last argument "writeFrame" to r9.
    // writeFrame is a flag used to indicate if this function
    // should write the pixel data to the frame buffer or not.
    // In a normal design, one would use the writeFrame as a
    // conditional flag. However, this function needs to sync with
    // the timing of a pixel data from the camera. Any difference in
    // logic like a branch will throw everything out of sync. Therefore
    // the logic below always write the pixel data to memory. In the case
    // where writeFrame is 1, the framebuf address points to a shared memory
    // frame buffer and the address gets incremented by 1 every loop.
    // In the case where writeFrame is 0, the memory pointer is some temporary
    // address on the stack and the address does not get incremented; the same
    // address is written over again each loop.
    _ASM(MOV    r9, r3)

    // fetch MAX_NEW_QVALS_PER_LINE value
    _ASM(LDR    r6, =MAX_NEW_QVALS_PER_LINE)
    _ASM(LDR    r7, [r6])
    _ASM(MOV    r10, r7)

    // fetch pixel threshold value
    _ASM(LDR    r6, =PIXEL_THRESHOLD)
    _ASM(LDR    r7, [r6])
    _ASM(MOV    r11, r7)

    // fetch max width
    _ASM(LDR    r6, =WIDTH)
    _ASM(LDR    r7, [r6])
    _ASM(MOV    r12, r7)

    // fetch INVALID_COL
    _ASM(LDR    r6, =INVALID_COL)
    _ASM(LDR    r7, [r6])
    _ASM(MOV    r8, r7)

    // clear current column value
    _ASM(MOVS   r3, #0)

    // reset col_start to INVALID_COL
    _ASM(MOV    r4, r8)

    // clear number of Q elements
    _ASM(MOVS   r5, #0)

     // create hsync bit mask
    _ASM(MOVS   r6, #0x1)
    _ASM(LSLS   r6, #11)

    _ASM(PUSH   {r0-r3})    // save args
    _ASM(BL.W   callSyncM1) // get pixel sync
    _ASM(POP    {r0-r3})    // restore args

    // pixel sync starts here

    // wait for hsync to go high
_ASM_LABEL(hsyncstart)
    _ASM(LDR    r7, [r0])
    _ASM(TST    r7, r6)
    _ASM(BEQ    hsyncstart)

    // variable delay --- get correct phase for sampling
    _ASM(NOP)
    _ASM(NOP)

    // *** PIXEL SYNC (start reading pixels)
    // Only interested in Red pixels. First pixel is Green ignore it.
    _ASM(LDRB   r7, [r0])   // 2; load Green pixel from GPIO
    _ASM(NOP)               // 1; ignoring Green pixel
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(B      loop_pixel) // 3

    // These labels below are for branches that have free cycles
    // and need to wait for the next pixel sync clock cycle.
_ASM_LABEL(sync_cycles_9)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_8)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_7)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_6)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_5)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_4)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_3)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_2)
    _ASM(NOP)               // 1
_ASM_LABEL(sync_cycles_1)
    _ASM(NOP)               // 1

_ASM_LABEL(loop_inc)
    // Increment column count and check end of line
    _ASM(ADDS   r3, #1)     // 1
    _ASM(CMP    r12, r3)    // 1
    _ASM(BEQ    eol)        // 1 if branch not taken; 3 otherwise

_ASM_LABEL(loop_pixel)
    _ASM(LDRB   r7, [r0])   // 2; load Red pixel from GPIO
    _ASM(STRB   r7, [r1])   // 2; store Red pixel to RAM
    _ASM(ADD    r1, r9)     // 1; move to next frame buffer pixel

    // Check pixel brightness is above threshold
    _ASM(CMP    r7, r11)     // 1
    _ASM(BGT    bright_pixel)// 1 or 3

    // If here then the pixel brightness is below the threshold.
    // If col_start is INVALID then else go back to beginning else
    // it's the end of a run-length; store it to memory.
    _ASM(NOP)               // 1
    _ASM(CMP    r4, r8)     // 1
    _ASM(BEQ    sync_cycles_9) // 1 or 3

    // Save col_start and col_end to values to RAM
    _ASM(STRH   r4, [r2])    // 2; col_start
    _ASM(STRH   r3, [r2, #2])// 2; col_end;  PIXEL_SYNC; ignore Green pixel

    // Increment Q count and Q memory location
    _ASM(ADDS   r5, #1)     // 1
    _ASM(ADDS   r2, #4)     // 1

    // reset col_start to INVALID_COL
    _ASM(MOV    r4, r8)     // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1

    // Check if q memory full
    _ASM(CMP    r5, r10)    // 1
    _ASM(BEQ    eol)        // 1

    // Loop back to top
    _ASM(B      loop_inc)   // 3

_ASM_LABEL(bright_pixel)
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1
    _ASM(NOP)               // 1

    // r3 = col_current
    // r4 = col_start
    // r8 = INVALID_COL

    // If col_start is valid then do nothing
    // Else it's a start of a new run-length; set col_start to col_current
    _ASM(CMP    r4, r8)     // 1; PIXEL SYNC; ignore Green pixel
    _ASM(BNE    sync_cycles_6) // 1 or 3
    _ASM(MOVS   r4, r3)     // 1; col_start = col_current
    _ASM(BNE    sync_cycles_3) // 3

// END of Loop

    // wait for hsync to go low
_ASM_LABEL(eol)
    _ASM(MOVS   r6, #0x1)
    _ASM(LSLS   r6, #11)
_ASM_LABEL(hsyncend)
    _ASM(LDR    r7, [r0])
    _ASM(TST    r7, r6)
    _ASM(BNE    hsyncend)

    // Set return value (number of Q values)
    _ASM(MOVS   r0, r5)

#ifdef KEIL
    _ASM(POP    {r1-r7, pc})
#else
    _ASM(POP    {r1-r7})
    _ASM(MOVS   r3, r0) // gcc is using r3 to return values?
#endif
    _ASM_END
}

int32_t getRLSFrame(void)
{
    // The pixy captures a frame every 20ms. Writing a frame to an SD Card
    // takes a little over 21ms. This means we can not write every frame.
    // Also, the framebuffer shared memory used to store the frame is shared by the M0
    // and M4 cores. When the M0 is in the time critical function "processLine" everything
    // must be deterministic in regards to timing. This means, if the M0 is writing the frame
    // pixels to the shared frame buffer, the M4 core should not access it during this time
    // or else the pixel sync timing will not align and the pixel data is invalid.
    static uint32_t s_frameCount = 0;
    uint32_t writeFrame = (s_frameCount++ % DIVIDER_FPS == 0);

    // If writing the pixels to the frame buffer then use the correct shared memory address.
    // Else use a dummy address on the stack. See comments in the processLine function for more details.
    uint8_t dummyFrameBuf;
    uint8_t *frameBuf = (writeFrame) ? (uint8_t*)MEM_M0_FRAME_LOC : &dummyFrameBuf;

    uint32_t numQvals;
    Qval qScratch[MAX_NEW_QVALS_PER_LINE];
    Qval lineBegin = {QVAL_LINE_BEGIN};

    // This waits for the current frame to finish to avoid partial frame.
    skipLines(0);

    for (uint32_t line = 0; line < CAM_RES2_HEIGHT; line++)
    {
        // not enough space--- return error
        if (qq_free() < MAX_NEW_QVALS_PER_LINE)
        {
            Qval frameError = {QVAL_FRAME_ERROR};
            qq_enqueue(&frameError);
            return -1;
        }
        qq_enqueue(&lineBegin);

        // Currently this only handles 320x200 resolution.
        // The first line of a Bayer Pattern is Blue and Green.
        // Start with second line with has Red pixels. Doesn't seem
        // to matter but if we are only taking one pixel then why not Red
        // for IR application.
        skipLine();

        numQvals = processLine((uint32_t *)&CAM_PORT, frameBuf, qScratch, writeFrame);

        if (writeFrame)
            frameBuf += CAM_RES2_WIDTH;

        for (uint32_t i = 0; i < numQvals; ++i)
        {
            qq_enqueue(&qScratch[i]);
        }
    }

    Qval frameEnd = {QVAL_FRAME_END};
    if (writeFrame) frameEnd.m_col_start |= QVAL_WRITE_FRAME_BIT;
    qq_enqueue(&frameEnd);
    return 0;
}

int rls_init(void)
{
    chirpSetProc("getRLSFrame", (ProcPtr)getRLSFrame);
    return 0;
}
