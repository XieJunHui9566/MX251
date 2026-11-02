/*-------------------------------------------------------------------------
   tancotf.c - Computes tan or cot of a 32-bit float as outlined in [1]

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
#include <stdbool.h>

#define P0  0.100000000E+1
#define P1 -0.958017723E-1
#define Q0  0.100000000E+1
#define Q1 -0.429135777E+0
#define Q2  0.971685835E-2

#define C1  1.5703125
#define C2  4.83826794897E-4

#define P(f,g) (P1*g*f+f)
#define Q(g) ((Q2*g+Q1)*g+Q0)

//A reasonable choice for YMAX is the integer part of B**(t/2)*PI/2:
#define YMAX 6433.0

#ifdef __SDCC_mcs251
#include "_mcs251_tan_reduce.h"
#endif


#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
/* 原分子/分母分别舍入后交回入口，不能用倒数乘法替换原除法。
 * 输出指针指向调用者的局部对象，不引入共享状态。 */
static float
mcs251_tancot_poly(float f, float *den) __reentrant __sdcccall(4)
{
    float g = f*f;
    float num = P(f,g);
    *den = Q(g);
    return num;
}
#endif

float tancotf(float x, bool iscotan)
{
    float f, g, xn, xnum, xden;
    int n;

#ifdef __SDCC_mcs251
    mcs251_tan_bits special;
    special.f = x;
    if ((special.u & 0x7fffffffUL) >= 0x7f800000UL)
    {
        if ((special.u & 0x7fffffffUL) == 0x7f800000UL)
            errno = EDOM;
        special.u |= 0x00400000UL;
        return special.f;
    }
    if ((special.u & 0x7fffffffUL) == 0)
    {
        if (!iscotan)
            return x;
        errno = ERANGE;
        special.u |= 0x7f800000UL;
        return special.f;
    }
    /* |x| < 2^-12 时 tan(x) 舍入为 x；避免除以 1 丢失次正规数。 */
    if (!iscotan && (special.u & 0x7fffffffUL) < 0x39800000UL)
        return x;
    n = mcs251_reduce_pio2(x, &f);
#else
    if (fabsf(x) > YMAX)
    {
        errno = ERANGE;
        return 0.0;
    }

    /*Round x*2*PI to the nearest integer*/
    n=(x*TWO_O_PI+(x>0.0?0.5:-0.5)); /*works for +-x*/
    xn=n;

    xnum=(int)x;
    xden=x-xnum;
    f=((xnum-xn*C1)+xden)-xn*C2;
#endif

    if (fabsf(f) < EPS)
    {
        xnum = f;
        xden = 1.0;
    }
    else
    {
#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
        xnum = mcs251_tancot_poly(f, &xden);
#else
        g = f*f;
        xnum = P(f,g);
        xden = Q(g);
#endif
    }

    if(n&1)
    //xn is odd
    {
        if(iscotan) return (-xnum/xden);
               else return (-xden/xnum);
    }
    else
    {
        if(iscotan) return (xden/xnum);
               else return (xnum/xden);
    }
}
