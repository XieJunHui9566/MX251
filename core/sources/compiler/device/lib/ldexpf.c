/*-------------------------------------------------------------------------
   ldexpf.c - Build a float from a mantisa and exponent.

   Copyright (C) 2001, 2002, Jesus Calvino-Fraga, jesusc@ieee.org

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library; see the file COPYING. If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.

   As a special exception, if you link this library with other files,
   some of which are compiled with SDCC, to produce an executable,
   this library does not by itself cause the resulting executable to
   be covered by the GNU General Public License. This exception does
   not however invalidate any other reasons why the executable file
   might be covered by the GNU General Public License.
-------------------------------------------------------------------------*/

/* Version 1.0 - Initial release */

#include <math.h>
#include <errno.h>

float ldexpf(float x, int pw2)
{
    union { float f; unsigned long u; } fl;
    unsigned long sign, fraction, discarded, halfway;
    int e;
    unsigned char shift;

    fl.f=x;
    e=(fl.u >> 23) & 0xff;
    if (!(fl.u & 0x7fffffffUL) || e==255 || !pw2)
        return x;

    sign=fl.u & 0x80000000UL;
    /* 先限制外部指数，避免 INT_MIN/INT_MAX 相加溢出；也适用于 int 为 32 位的端口。 */
    if (pw2>300)
        goto overflow;
    if (pw2 < -300)
        goto underflow_zero;

    fraction=fl.u & 0x007fffffUL;
    if (!e)
    {
        e=1;
        do { fraction <<= 1; --e; } while (!(fraction & 0x00800000UL));
    }
    else
        fraction |= 0x00800000UL;
    e+=pw2;
    if (e>254)
        goto overflow;
    if (e>0)
    {
        fl.u=sign | ((unsigned long)e << 23) | (fraction & 0x007fffffUL);
        return fl.f;
    }
    if (e < -23)
        goto underflow_zero;

    /* 只有 1..24 位右移；直接对有效数舍入，避免双重舍入或丢弃粘滞位。
       软件浮点无动态舍入环境，此处固定为最近值、平局取偶数。 */
    shift=1-e;
    halfway=1UL << (shift-1);
    discarded=fraction & ((halfway << 1)-1UL);
    fraction >>= shift;
    if (discarded>halfway || (discarded==halfway && (fraction & 1UL)))
        ++fraction;
    /* 舍入后仍微小且不精确才报告下溢；精确次正规数不改 errno。 */
    if (discarded && fraction<0x00800000UL)
        errno=ERANGE;
    fl.u=sign | fraction;
    return fl.f;

overflow:
    /* 遵守本库 HUGE_VALF 的定义；不在这里改变其它数学函数的溢出约定。 */
    errno=ERANGE;
    fl.f=HUGE_VALF;
    fl.u |= sign;
    return fl.f;

underflow_zero:
    errno=ERANGE;
    fl.u=sign;
    return fl.f;
}
