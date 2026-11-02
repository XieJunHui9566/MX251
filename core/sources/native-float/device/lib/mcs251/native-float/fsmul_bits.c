/*-------------------------------------------------------------------------
   _fsmul.c - Floating point library in optimized assembly for 8051

   Copyright (c) 2004, Paul Stoffregen, paul@pjrc.com

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


/* R38：保留原 C fallback 的精确 48 位乘积与最终一次最近偶数舍入。
 * 输入/输出改用 binary32 位模式；ABI4 的参数 DR4/DR12 和返回 DR4 不变。
 * 正常数汇编快路径继续使用父版本的机器指令，不以精度或吞掉边界换体积。
 */
/* 24x24 位精确乘积拆成 32+16 位，不调用 64 位运行库。
 * 只在最终精度处做一次最近偶数舍入，次正规输出也不先舍入成正规数。
 * 与本端口除法一致：保留首个 NaN 的 payload 并 quiet；无 fenv 标志。
 */
unsigned long __fsmul_bits(unsigned long x, unsigned long y) __reentrant __sdcccall(4)
{
  unsigned long a, b, hi, cross;
  unsigned int low_product;
  unsigned char lo;
  unsigned long sign;
  int ea, eb, exp;

  a = x; b = y;
  sign = (a ^ b) & 0x80000000UL;
  ea = (int)(unsigned char)(((unsigned int)(a >> 16) * 2U) >> 8);
  eb = (int)(unsigned char)(((unsigned int)(b >> 16) * 2U) >> 8);
  /* 正规数仅检查指数；payload/零判断只留在特殊路径。 */
  if (ea == 255 || eb == 255)
    {
      if (ea == 255 && (a & 0x007fffffUL)) { x = a | 0x00400000UL; return x; }
      if (eb == 255 && (b & 0x007fffffUL)) { y = b | 0x00400000UL; return y; }
      if (!(a & 0x7fffffffUL)) return 0x7fc00000UL;
      if (!(b & 0x7fffffffUL)) return 0x7fc00000UL;
      x = sign | 0x7f800000UL;
      return x;
    }
  a &= 0x007fffffUL; b &= 0x007fffffUL;
  if (ea) a |= 0x00800000UL;
  else
    {
      if (!a) { x = sign; return x; }
      ea = 1; do { a <<= 1; --ea; } while (a < 0x00800000UL);
    }
  if (eb) b |= 0x00800000UL;
  else
    {
      if (!b) { x = sign; return x; }
      eb = 1; do { b <<= 1; --eb; } while (b < 0x00800000UL);
    }
  exp = ea + eb - 127;

  /* a=256*ah+al。四个 16x16 乘积均精确，cross 最大 25 位。
   * 低 16 位只在最终舍入时作为 sticky，但形成 hi 时不能丢进位。
   */
  /* 显式保留 32 位乘积；unsigned int 在此目标为 16 位。
   * 宽 MIR 直接生成原生乘法，不再借助四次函数调用隔离操作。 */
  hi = (unsigned long)(unsigned int)(a >> 8) * (unsigned int)(b >> 8);
  low_product = (unsigned int)(a & 0xffUL) * (unsigned int)(b & 0xffUL);
  cross = (unsigned long)(unsigned int)(a & 0xffUL) * (unsigned int)(b >> 8);
  cross += (unsigned long)(unsigned int)(b & 0xffUL) * (unsigned int)(a >> 8);
  cross += low_product >> 8;
  /* 先将低乘积的进位加入 cross，再合并两低字节；后续只查询 sticky 非零。 */
  lo = (unsigned char)(cross | low_product);
  hi += cross >> 8;

  if (hi > 0x7fffffffUL) { lo |= (hi & 1UL); hi >>= 1; ++exp; }
  if (exp >= 255) { x = sign | 0x7f800000UL; return x; }
  if (exp <= 0)
    {
      if (exp < -23) { x = sign; return x; }
      /* 最多 24 步；被移出的任何非零位都保留在 sticky 中。 */
      do { lo |= (hi & 1UL); hi >>= 1; ++exp; } while (exp <= 0);
      exp = 0;
    }
  if ((hi & 0x7fUL) > 0x40UL ||
      ((hi & 0x7fUL) == 0x40UL && ((hi & 0x80UL) || lo))) hi += 0x80UL;
  hi >>= 7;
  /* 正规尾数含隐藏位：直接相加也覆盖进位到下一指数/无穷大的情况。 */
  if (exp) x = sign | (((unsigned long)((unsigned int)(exp - 1) << 7) << 16) + hi);
  else x = sign | hi;
  return x;
}

