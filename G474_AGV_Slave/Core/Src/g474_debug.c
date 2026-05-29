/* g474_debug.c */
#include "g474_debug.h"
#include <stdio.h>

extern UART_HandleTypeDef hlpuart1;

#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

void G474_Debug_Init(void)
{
    printf("\r\n[G474] Boot, UART1 115200 OK\r\n");
}
