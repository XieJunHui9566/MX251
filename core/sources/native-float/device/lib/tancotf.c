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
#include "_mcs251_math_bits.h"
/* 只让多项式自变量跨算术调用存活，外层符号和约简状态留在调用者。
 * 保留原常量及每次运算的次序、精度和异常行为。 */
static uint32_t mcs251_tancot_denominator(uint32_t g) MATH_ABI
{
    return M_ADD(M_MUL(M_ADD(M_MUL(MB_TC_Q2,g),MB_TC_Q1),g),MB_TC_Q0);
}

uint32_t mcs251_tancot_bits(uint32_t x, unsigned char iscotan) MATH_ABI
{
    uint32_t f,g,num,den,tmp;
    unsigned char n;
    if(M_ABS(x)>=0x7f800000UL)
    {
        if(M_ABS(x)==0x7f800000UL) errno=EDOM;
        return x|0x00400000UL;
    }
    if(M_ABS(x)==0)
    {
        if(!iscotan) return x;
        errno=ERANGE;
        return x|0x7f800000UL;
    }
    if(!iscotan && M_ABS(x)<0x39800000UL) return x;
    if(M_ABS(x)<=0x3f000000UL) { f=x; n=0; }
    else
    {
        n=mcs251_reduce_fraction(x,&f);
        f=M_MUL(f,MB_REDUCE_PI2);
    }
    /* 清符号后按正binary32幅值排序；NaN幅值大于Inf，仍不进入此分支。 */
    if(M_ABS(f)<MB_EPS) { num=f; den=MB_ONE; }
    else
    {
        g=M_MUL(f,f);
        num=M_ADD(M_MUL(M_MUL(MB_TC_P1,g),f),f);
        den=mcs251_tancot_denominator(g);
    }
    if((n&1)^iscotan) { tmp=num; num=den; den=tmp; }
    if(n&1) num=M_NEG(num);
    return M_DIV(num,den);
}

#else
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

#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
    /* 先按象限和 cot 选择有序操作数，再仅对分子取负。
       保留除法前的符号处理及 NaN/零语义，不改为商取负或倒数乘法。 */
    if ((n & 1) ^ iscotan)
    {
        f = xnum;
        xnum = xden;
        xden = f;
    }
    if (n & 1)
        xnum = -xnum;
    return xnum/xden;
#else
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
#endif
}

#endif
