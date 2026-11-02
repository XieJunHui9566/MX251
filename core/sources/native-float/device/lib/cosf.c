/*-------------------------------------------------------------------------
   cosf.c - Computes cos(x) where x is a 32-bit float.

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
#include <stdbool.h>

#if defined(__SDCC_mcs251) && defined(__SDCC_MCS251_NATIVE_RUNTIME)
#include "_mcs251_math_bits.h"
/* 构建配方将原float ABI4符号同址绑定于此整数位型入口。
 * 单个32位参数/返回均为DR4；原API原型、特殊域和逐次舍入不变。 */
uint32_t mcs251_cos_bits(uint32_t x) MATH_ABI
{
    /* 原公开入口把正负零都折叠为同一个结果；NaN不满足此测试。 */
    if (M_ABS(x)==0) return MB_ONE;
    return mcs251_sincos_bits(x, 1);
}

#else
float sincosf(float x, bool iscos);

float cosf(float x) _FLOAT_FUNC_REENTRANT _MCS251_NATIVE_MATH
{
    if (x==0.0) return 1.0;
    return sincosf(x, 1);
}

#endif
