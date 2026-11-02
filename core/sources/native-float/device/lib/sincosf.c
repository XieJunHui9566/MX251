/*-------------------------------------------------------------------------
   sincosf.c - Computes sin or cos of a 32-bit float as outlined in [1]

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

#define r1      -0.1666665668E+0
#define r2       0.8333025139E-2
#define r3      -0.1980741872E-3
#define r4       0.2601903036E-5

/* PI=C1+C2 */
#define C1       3.140625
#define C2       9.676535897E-4

/*A reasonable value for YMAX is the int part of PI*B**(t/2)=3.1416*2**(12)*/
#define YMAX     12867.0

#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
#include "_mcs251_math_bits.h"
/* 只让多项式自变量跨算术调用存活，外层符号和约简状态留在调用者。
 * 保留原常量及每次运算的次序、精度和异常行为。 */
static uint32_t mcs251_sincos_polynomial(uint32_t g) MATH_ABI
{
    return M_MUL(M_ADD(M_MUL(M_ADD(M_MUL(M_ADD(M_MUL(MB_SC_R4,g),MB_SC_R3),g),MB_SC_R2),g),MB_SC_R1),g);
}

/* 纯约简 helper 只接收 y/xn，不读取或写入外层符号、iscos、errno 状态。
 * 根据下述精确性边界消去整数拆分，公共范围门与异常政策保持不变。 */
static uint32_t mcs251_sincos_reduce(uint32_t y, uint32_t xn) MATH_ABI
{
    /* 前置条件来自同文件范围门：有限 y=abs(x)<=12867，xn 是有界整数或半整数。
     * C1=201/64，故 xn*C1 以及 floor(y)-xn*C1 都可精确表示。
     * y-floor(y) 也精确；原先两项相加与直接 y-xn*C1 的唯一舍入相同。
     * 非有限输入只可能是已取绝对值的 NaN，仍传播相同符号/载荷并静默化。
     * C2 乘法及最后减法保持原顺序；不放宽公共输入域或启用 fast-math。 */
    uint32_t f=M_SUB(y,M_MUL(xn,MB_SC_C1));
    return M_SUB(f,M_MUL(xn,MB_SC_C2));
}

uint32_t mcs251_sincos_bits(uint32_t x, unsigned char iscos) MATH_ABI
{
    uint32_t y,f,r,g,xn;
    uint32_t n;
    unsigned char sign;
    if(iscos) { y=M_ADD(M_ABS(x),MB_HALF_PI); sign=0; }
    /* 无符号模 2^32 区间精确覆盖 [0x80000001,0xff800000]：
     * 即负有限非零数与负无穷；排除 -0、+0 以及两种符号的所有 NaN。
     * 因而与既有 bits_lt(x,0) 的严格小于语义一致，不能简化为单测符号位。 */
    else if(x-0x80000001UL<0x7f800000UL) { y=M_NEG(x); sign=1; }
    else { y=x; sign=0; }
    if(((uint32_t)(y-(MB_YMAX+1UL))<(0x7f800000UL-MB_YMAX))) { errno=ERANGE; return 0; }
    /* 上方范围门已排除无穷大，有限y<=12867使本式结果<=4097。
     * 剩余NaN按既有bits_f2s政策只得到-1/+1，均在int16域内。
     * 两次窄化再符号扩展均是恒等映射，直接保留原32位转换的位型。 */
    n=bits_f2s(M_ADD(M_MUL(y,MB_IPI),MB_HALF));
    /* sign 始终为0/1；异或保留规范化语义并避免转为独立bool对象。 */
    if(n&1) sign^=1;
    xn=bits_s2f(n);
    if(iscos) xn=M_SUB(xn,MB_HALF);
    y=M_ABS(x);
    /* 这里的|x|同样有界；NaN先清符号后按原政策得到-1。 */
    f=mcs251_sincos_reduce(y,xn);
    g=M_MUL(f,f);
    if(((uint32_t)(g-(MB_EPS2+1UL))<(0x7f800000UL-MB_EPS2)))
    {
        r=mcs251_sincos_polynomial(g);
        f=M_ADD(f,M_MUL(f,r));
    }
    return sign?M_NEG(f):f;
}

#else
#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
/* 仅迁移约简后的多项式。保持原常量和逐次舍入顺序；范围、阈值、
 * 符号和 errno 仍由原入口处理，不在此引入近似或融合运算。 */
static float
mcs251_sincos_poly(float f, float g) __reentrant __sdcccall(4)
{
    float r = (((r4*g+r3)*g+r2)*g+r1)*g;
    return f+f*r;
}
#endif

float sincosf(float x, bool iscos)
{
    float y, f, r, g, XN;
    int N;
    bool sign;

    if(iscos)
    {
        y=fabsf(x)+HALF_PI;
        sign=0;
    }
    else
    {
        if(x<0.0)
            { y=-x; sign=1; }
        else
            { y=x; sign=0; }
    }

    if(y>YMAX)
    {
        errno=ERANGE;
        return 0.0;
    }

    /*Round y/PI to the nearest integer*/
    N=((y*iPI)+0.5); /*y is positive*/

    /*If N is odd change sign*/
    if(N&1) sign=!sign;

    XN=N;
    /*Cosine required? (is done here to keep accuracy)*/
    if(iscos) XN-=0.5;

    y=fabsf(x);
    r=(int)y;
    g=y-r;
    f=((r-XN*C1)+g)-XN*C2;

    g=f*f;
    if(g>EPS2) //Used to be if(fabsf(f)>EPS)
    {
#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
        f=mcs251_sincos_poly(f,g);
#else
        r=(((r4*g+r3)*g+r2)*g+r1)*g;
        f+=f*r;
#endif
    }
    return (sign?-f:f);
}

#endif
