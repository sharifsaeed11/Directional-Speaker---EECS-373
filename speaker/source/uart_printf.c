/******************************************************************************
* File Name:   uart_printf.c
*
* Description: Sends printf() output to the debug UART (USIC0 CH0, 115200 8N1).
*              On the XMC4700 Relax Kit this UART is connected to the on-board
*              J-Link debugger, which shows up on your computer as a USB serial
*              (COM) port, so you can watch printf() in any serial terminal.
*
*              The UART itself is already set up by cybsp_init() (the BSP).
*              newlib's printf() calls _write() for every chunk of output;
*              the BSP's default _write() is an empty stub that halts, so we
*              provide our own here.
*
* EECS 373 example project.
******************************************************************************/

#include "cybsp.h"
#include "xmc_uart.h"

int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++)
    {
        if (ptr[i] == '\n')
        {
            XMC_UART_CH_Transmit(CYBSP_DEBUG_UART_HW, (uint16_t)'\r'); /* terminals want CR+LF */
        }
        XMC_UART_CH_Transmit(CYBSP_DEBUG_UART_HW, (uint16_t)ptr[i]);
    }
    return len;
}

/* Tell newlib that stdout is a terminal (the default stub would halt). */
int _isatty(int file)
{
    (void)file;
    return 1;
}

/* [] END OF FILE */
