/*-------------------------------------------------------------------------
   sqrtf.c - Computes square root of a 32-bit float as outlined in [1]

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

/* [1] William James Cody and W.  M.  Waite.  _Software manual for the
   elementary functions_, Englewood Cliffs, N.J.:Prentice-Hall, 1980. */

/* Version 1.0 - Initial release */

#include <math.h>
#include <errno.h>

/* The MCS251 integer recurrence is adapted from Sun's e_sqrtf.c:
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 * Float conversion: Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */
#if defined(__SDCC_mcs251)
float sqrtf(float x) _FLOAT_FUNC_REENTRANT _MCS251_NATIVE_MATH
{
    union { float f; unsigned long u; } value;
    unsigned long remainder, root, bit, trial;
    int exponent;

    value.f=x;
    remainder=value.u & 0x7fffffffUL;
    /* 先分类，避免软件浮点比较；保留 NaN payload 和正负零。
       负实数继续使用本库既有的 0 / EDOM 约定。 */
    if (!remainder || remainder>0x7f800000UL)
        return x;
    if (value.u & 0x80000000UL)
    {
        errno=EDOM;
        return 0.0f;
    }
    if (remainder==0x7f800000UL || remainder==0x3f800000UL)
        return x;

    exponent=(remainder >> 23) & 0xff;
    if (!exponent)
    {
        exponent=1;
        do { remainder <<= 1; --exponent; } while (!(remainder & 0x00800000UL));
    }
    exponent-=127;
    remainder=(remainder & 0x007fffffUL) | 0x00800000UL;
    /* 先改成偶数指数，再做 /2，避免依赖负有符号数右移的实现定义行为。 */
    if (exponent & 1)
    {
        remainder <<= 1;
        --exponent;
    }
    exponent/=2;
    remainder <<= 1;
    root=0;
    /* 25 次迭代生成 24 位有效数及一位舍入信息，全部中间量小于 2^28。 */
    for (bit=0x01000000UL; bit; bit >>= 1)
    {
        /* partial 始终等于 2*root，直接推导以减少循环内栈变量。 */
        trial=(root << 1)+bit;
        if (trial<=remainder)
        {
            remainder-=trial;
            root+=bit;
        }
        remainder <<= 1;
    }
    /* 固定最近值舍入。二进制输入的非精确平方根不会恰好落在中点；
       非零余数提供粘滞信息，root 最低位为舍入位。 */
    if (remainder)
        root+=root & 1UL;
    /* 先转无符号再移位，负指数也无有符号移位未定义行为。
       非零有限 binary32 的平方根均为正规数，不会产生输出上下溢。 */
    value.u=(root >> 1)+0x3f000000UL+((unsigned long)exponent << 23);
    return value.f;
}
#else
float sqrtf(float x) _FLOAT_FUNC_REENTRANT _MCS251_NATIVE_MATH
{
    float f, y;
    int n;

    if (x==0.0) return x;
    else if (x==1.0) return 1.0;
    else if (x<0.0)
    {
        errno=EDOM;
        return 0.0;
    }
    f=frexpf(x, &n);
    y=0.41731+0.59016*f; /*Educated guess*/
    /*For a 24 bit mantisa (float), two iterations are sufficient*/
    y+=f/y;
    y=ldexpf(y, -2) + f/y; /*Faster version of 0.25 * y + f/y*/

    if (n&1)
    {
        y*=0.7071067812;
        ++n;
    }
    return ldexpf(y, n/2);
}

#endif
