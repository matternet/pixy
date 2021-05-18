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

#include <string.h>
#include "serial.h"
#include "spi.h"
#include "i2c.h"
#include "uart.h"
#include "pixy_init.h"
#include "exec.h"


enum SerialRecvState
{
    RECV_STATE_INIT,
    RECV_STATE_SYNC,
    RECV_STATE_CMD,
};

static uint8_t g_interface = 0;
static Iserial *g_serial = 0;
static SerialCmdCallback g_cmdCallback = NULL;
static SerialRecvState g_state = RECV_STATE_INIT;


int ser_init(SerialCallback callback, SerialCmdCallback cmdCallback)
{
    i2c_init(callback);
    spi_init(callback);
    uart_init(callback);

    g_i2c0->setSlaveAddr(I2C_DEFAULT_SLAVE_ADDR);
    g_uart0->setBaudrate(SER_INTERFACE_SER_BAUD);
    ser_setInterface(SER_INTERFACE_I2C);

    g_cmdCallback = cmdCallback;
    return 0;
}

void ser_flush()
{
    uint8_t c;
    while(g_serial->receive(&c, 1));
}

int ser_setInterface(uint8_t interface)
{
    if (interface>SER_INTERFACE_LEGO)
        return -1;

    if (g_serial!=NULL)
        g_serial->close();

    g_interface = interface;

    switch (interface)
    {
    case SER_INTERFACE_SS_SPI:
        g_serial = g_spi;
        g_spi->setAutoSlaveSelect(false);
        break;

    case SER_INTERFACE_I2C:
        g_serial = g_i2c0;
        g_i2c0->setFlags(false, true);
        break;

    case SER_INTERFACE_UART:
        g_serial = g_uart0;
        break;

    case SER_INTERFACE_LEGO:
        g_serial = g_i2c0;
        g_i2c0->setSlaveAddr(0x01);
        g_i2c0->setFlags(true, false);
        break;

    default:
    case SER_INTERFACE_ARDUINO_SPI:
        g_serial = g_spi;
        g_spi->setAutoSlaveSelect(true);
        break;
    }

    g_serial->open();

    return 0;
}

uint8_t ser_getInterface()
{
    return g_interface;
}

Iserial *ser_getSerial()
{
    return g_serial;
}

void ser_update()
{
    g_serial->update();
}

void ser_processInput()
{
    uint8_t byte;

    switch (g_state)
    {
    case RECV_STATE_INIT:
        g_state = RECV_STATE_SYNC;
        break;

    case RECV_STATE_SYNC:
        if (g_serial->receive(&byte, 1) && byte == SER_SYNC_BYTE)
        {
            g_state = RECV_STATE_CMD;
        }
        break;

    case RECV_STATE_CMD:
        if (g_serial->receive(&byte, 1) && g_cmdCallback)
        {
            g_cmdCallback(byte, NULL, 0);
        }
        g_state = RECV_STATE_INIT;
        break;

    default:
        g_state = RECV_STATE_INIT;
        break;
    }
}
