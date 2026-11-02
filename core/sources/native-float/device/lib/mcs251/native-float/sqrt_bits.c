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


/* The MCS251 integer recurrence is adapted from Sun's e_sqrtf.c:
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 * Float conversion: Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */
/* R37：参数和结果均为 binary32 位模式，ABI4 与原 sqrtf 相同。
 * 保留 NaN 位模式（包括 signaling 位）和负实数返回 0 / EDOM 的旧库契约。
 * 错误路径独立编译，使核心不依赖 errno 的存储空间，也不绕过 ABI 保存规则。
 */
unsigned long __sqrt_domain(void) __reentrant __sdcccall(4);

unsigned long __sqrt_bits(unsigned long x) __reentrant __sdcccall(4)
{
    unsigned long remainder, root, bit, trial;
    unsigned int exponent;

    remainder = x & 0x7fffffffUL;
    if (!remainder || remainder > 0x7f800000UL)
        return x;
    if (x != remainder)
        return __sqrt_domain();
    if (remainder == 0x7f800000UL || remainder == 0x3f800000UL)
        return x;

    /* 原无偏指数 e 加 254 后范围为 105..381，全程用非负整数。
     * 这样 floor(e/2)+127 等于 exponent/2，不需要有符号除法和右移。
     */
    exponent = (unsigned int)(remainder >> 23);
    if (!exponent)
    {
        exponent = 128;
        do { remainder <<= 1; --exponent; }
        while (remainder < 0x00800000UL);
    }
    else
        exponent += 127;
    remainder = (remainder & 0x007fffffUL) | 0x00800000UL;
    if (exponent & 1U)
        remainder <<= 1;
    exponent = (exponent >> 1) - 1U;
    remainder <<= 1;
    root = 0;
    /* 25 次有界迭代；中间量仍小于 2^28，保留原整数递推与舍入。 */
    for (bit = 0x01000000UL; bit; bit >>= 1)
    {
        trial = (root << 1) + bit;
        if (trial <= remainder)
        {
            remainder -= trial;
            root += bit;
        }
        remainder <<= 1;
    }
    if (remainder)
        root += root & 1UL;
    return (root >> 1) + ((unsigned long)exponent << 23);
}
