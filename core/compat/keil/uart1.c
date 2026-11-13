#include "uart1.h"

__sfr __at(0x99) mx_uart1_data;
__sbit __at(0x98) mx_uart1_rx_ready;
__sbit __at(0x99) mx_uart1_tx_ready;

static void uart1_send(unsigned char c)
{
    if (mx_uart1_rx_ready && mx_uart1_data == 0x13) {
        do {
            mx_uart1_rx_ready = 0;
            while (!mx_uart1_rx_ready) {}
        } while (mx_uart1_data != 0x11);
        mx_uart1_rx_ready = 0;
    }
    while (!mx_uart1_tx_ready) {}
    mx_uart1_tx_ready = 0;
    mx_uart1_data = c;
}

int putchar(int c)
{
    if ((unsigned char)c == '\n') uart1_send('\r');
    uart1_send((unsigned char)c);
    return (unsigned char)c;
}

char _getkey(void)
{
    unsigned char c;
    while (!mx_uart1_rx_ready) {}
    c = mx_uart1_data;
    mx_uart1_rx_ready = 0;
    return c;
}
