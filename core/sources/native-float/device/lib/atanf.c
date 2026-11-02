/*-------------------------------------------------------------------------
   atanf.c - Computes arctan of a 32-bit float as outlined in [1]

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

#define P0 -0.4708325141E+0
#define P1 -0.5090958253E-1
#define Q0  0.1412500740E+1
#define Q1  0.1000000000E+1

#define P(g,f) ((P1*g+P0)*g*f)
#define Q(g) (Q1*g+Q0)

#define K1  0.2679491924 /* 2-sqrt(3) */
#define K2  0.7320508076 /* sqrt(3)-1 */
#define K3  1.7320508076 /* sqrt(3)   */

#ifdef __SDCC_mcs51
   #define myconst __code
#else
   #define myconst const
#endif

#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
#include "_mcs251_math_bits.h"
/* 整数仅传送位型；算术仍逐次调用原 binary32 核心，不合并舍入。
 * Q1 为 1，原目标编译已把 Q1*g 消去；这里保留实际父版运算依赖。
 * 分阶段先求分子 r，再求分母并相除，缩短分母存活区间；每个 binary32
 * 算术调用和各自舍入仍独立保留，不使用融合运算或代数恒等变换。 */
static uint32_t mcs251_atan_poly(uint32_t f) MATH_ABI
{
    uint32_t g=M_MUL(f,f);
    uint32_t r=M_MUL(MB_AT_P1,g);
    r=M_ADD(r,MB_AT_P0);
    r=M_MUL(r,g);
    r=M_MUL(r,f);
    r=M_DIV(r,M_ADD(g,MB_AT_Q0));
    return M_ADD(f,r);
}

/* 原公开 float atanf 声明仍在 math.h。构建配方把原 ABI4 符号绑定到
 * 此同址入口：单个 32 位参数及返回均为 DR4，不生成额外包装或转换。 */
uint32_t mcs251_atan_bits(uint32_t x) MATH_ABI
{
    uint32_t f,r,g;
    /* n 的低两位保存区域号 0..3，bit2 保存原始有序负号；两部分互不覆盖。 */
    unsigned char n=0;
    /* [负最小次正规,负无穷] 满足有序 x<0；负零和所有 NaN 排除。 */
    if(x-0x80000001UL<0x7f800000UL) n=4;
    f=M_ABS(x);
    /* abs 后非负位型的 (1,+Inf] 闭区间，NaN 不参与区间变换。 */
    if(f-(MB_ONE+1UL)<(0x7f800000UL-MB_ONE)) { f=M_DIV(MB_ONE,f); n|=2; }
    /* 此处 f 为 [0,1] 或正 NaN；+Inf 已经由原除法变为 +0。 */
    if(f-(MB_AT_K1+1UL)<(MB_ONE-MB_AT_K1))
    {
        f=M_DIV(M_ADD(M_SUB(M_MUL(MB_AT_K2,f),MB_ONE),f),M_ADD(MB_AT_K3,f));
        ++n;
    }
    /* 正浮点位型随数值单调；NaN/Inf 均大于有限 EPS。 */
    if(M_ABS(f)<MB_EPS) r=f;
    else
    {
        r=mcs251_atan_poly(f);
    }
    if(n&2) r=M_NEG(r);
    /* 仅以 n 的低两位选择原区域常量；bit2 只在最终符号恢复时读取。
     * 显式分支避免常量表索引物化，不把打包后的 n 当作数组下标。 */
    if(n&1) g=(n&2)?MB_AT_A3:MB_AT_A1;
    else g=(n&2)?MB_AT_A2:MB_AT_A0;
    r=M_ADD(r,g);
    if(n&4) r=M_NEG(r);
    return r;
}

#else
float atanf(float x) _FLOAT_FUNC_REENTRANT _MCS251_NATIVE_MATH
{
    float f, r, g;
    int n=0;
	static myconst float a[]={  0.0, 0.5235987756, 1.5707963268, 1.0471975512 };

    f=fabsf(x);
    if(f>1.0)
    {
        f=1.0/f;
        n=2;
    }
    if(f>K1)
    {
        f=((K2*f-1.0)+f)/(K3+f);
        // What it is actually wanted is this more accurate formula,
        // but SDCC optimizes it and then it does not work:
        // f=(((K2*f-0.5)-0.5)+f)/(K3+f);
        n++;
    }
    if(fabsf(f)<EPS) r=f;
    else
    {
        g=f*f;
        r=f+P(g,f)/Q(g);
    }
    if(n>1) r=-r;
    r+=a[n];
    if(x<0.0) r=-r;
    return r;
}

#endif
