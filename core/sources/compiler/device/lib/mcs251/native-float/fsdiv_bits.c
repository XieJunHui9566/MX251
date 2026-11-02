/*-------------------------------------------------------------------------
   _fsdiv.c - Floating point library in optimized assembly for 8051

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


/* R35：binary32 位模式接口，与浮点 ABI4 具有相同的两个 DR 参数和 DR 返回值。
 * 保留原除法的 NaN、带符号零、次正规数和最近偶数舍入策略。
 * 不含静态工作区、不读写硬件数学单元；中断重入按 ABI4 保存现场。
 */
unsigned long __fsdiv_bits(unsigned long x, unsigned long y) __reentrant __sdcccall(4)
{
  unsigned long a, b, sign, q;
  int ea, eb, exp;
  unsigned char bits;

  a = x & 0x7fffffffUL;
  b = y & 0x7fffffffUL;
  sign = (x ^ y) & 0x80000000UL;

  if (a > 0x7f800000UL) { x |= 0x00400000UL; return x; }
  if (b > 0x7f800000UL) { y |= 0x00400000UL; return y; }
  if ((a == 0 && b == 0) || (a == 0x7f800000UL && b == 0x7f800000UL))
    { x = 0x7fc00000UL; return x; }
  if (a == 0x7f800000UL || b == 0)
    { x = sign | 0x7f800000UL; return x; }
  if (a == 0 || b == 0x7f800000UL)
    { x = sign; return x; }

  ea = (int)(a >> 23);
  eb = (int)(b >> 23);
  a &= 0x007fffffUL;
  b &= 0x007fffffUL;
  if (ea) a |= 0x00800000UL;
  else
    {
      ea = 1;
      while (a < 0x00800000UL) { a <<= 1; --ea; }
    }
  if (eb) b |= 0x00800000UL;
  else
    {
      eb = 1;
      while (b < 0x00800000UL) { b <<= 1; --eb; }
    }
  exp = ea - eb + 127;
  if (a < b) { a <<= 1; --exp; }
  if (exp >= 255) { x = sign | 0x7f800000UL; return x; }
  if (exp < -23) { x = sign; return x; }
  /* 最小非规格化数的一半是偶数零；大于一半才进到最小非规格化数。 */
  if (exp == -23) { x = sign | (a > b ? 1UL : 0UL); return x; }

  /* 按最终精度直接求商，避免先舍入正规尾数再缩小造成双重舍入。 */
  bits = exp > 0 ? 24 : (unsigned char)(23 + exp);
  q = 0;
  do
    {
      q <<= 1;
      if (a >= b) { a -= b; q |= 1UL; }
      a <<= 1;
    }
  while (--bits);
  /* a 已是余数的两倍，能直接区分小于、等于或大于半个 ULP。 */
  if (a > b || (a == b && (q & 1UL))) ++q;
  if (exp <= 0) x = sign | q;
  else
    {
      if (q == 0x01000000UL) { q >>= 1; ++exp; }
      if (exp >= 255) x = sign | 0x7f800000UL;
      else x = sign | ((unsigned long)exp << 23) | (q & 0x007fffffUL);
    }
  return x;
}
