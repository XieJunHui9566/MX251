/* Trace callbacks use the same ordinary-wide backend as the SDK's LCD unit.
 * The UART/timer harness is kept in its separate legacy translation unit. */
#include "machine-board.h"
u16 POINT_COLOR;
u32 trace;
static void record(u16 value) { trace = (trace << 5) ^ trace ^ value; }
void LCD_Fill(u16 x,u16 y,u16 right,u16 bottom,u16 color)
{
    record(x); record(y); record(right); record(bottom); record(color);
}
