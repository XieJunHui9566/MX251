/*-------------------------------------------------------------------------
   frexpf.c - Returns the exponent and mantisa of a 32 bit float.

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

float frexpf(float x, int *pw2)
{
    union { float f; unsigned long u; } fl;
    unsigned long fraction;
    int e;

    fl.f=x;
    e=(fl.u >> 23) & 0xff;
    *pw2=0;
    /* 零必须保留符号；Inf/NaN 不改位型，未指定的输出指数统一置零。 */
    if (!(fl.u & 0x7fffffffUL) || e==255)
        return x;

    if (!e)
    {
        fraction=fl.u & 0x007fffffUL;
        e=1;
        /* 非零 binary32 次正规数最多左移 23 次，不依赖浮点乘法库。 */
        do { fraction <<= 1; --e; } while (!(fraction & 0x00800000UL));
        fl.u=(fl.u & 0x80000000UL) | fraction;
    }
    *pw2=e-126;
    fl.u=(fl.u & 0x807fffffUL) | 0x3f000000UL;
    return fl.f;
}
