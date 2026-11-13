/*-------------------------------------------------------------------------
   modff.c - Returns both the integer and fraction of a float.

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

float modff(float x, float *y)
{
    union { float f; unsigned long u; } value;
    unsigned long magnitude, sign, fraction, mask;
    unsigned char exponent;

    value.f = x;
    sign = value.u & 0x80000000UL;
    magnitude = value.u & 0x7fffffffUL;
    if (magnitude >= 0x4b000000UL)
    {
        *y = x;
        if (magnitude > 0x7f800000UL)
            return x;
        value.u = sign;
        return value.f;
    }
    if (magnitude < 0x3f800000UL)
    {
        value.u = sign;
        *y = value.f;
        return x;
    }
    exponent = (unsigned char)(magnitude >> 23);
    mask = (1UL << (150 - exponent)) - 1;
    fraction = value.u & mask;
    value.u &= ~mask;
    *y = value.f;
    if (fraction)
    {
        /* The fraction is exact. Normalize its integer significand without
           pulling floating subtraction or integer conversions into the link. */
        while (!(fraction & 0x00800000UL))
        {
            fraction <<= 1;
            --exponent;
        }
        value.u = sign | ((unsigned long)exponent << 23) | (fraction & 0x007fffffUL);
    }
    else
        value.u = sign;
    return value.f;
}
