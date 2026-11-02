/* R43：binary32 加法，使用整数位模式和 ABI4；不访问硬件数学单元。
 * 精确对齐保留 guard/round/sticky，最终仅做一次最近偶数舍入。
 * 无静态工作区，不读写 errno；不提供动态舍入或 fenv 异常标志。
 */
unsigned long __fsadd_bits(unsigned long x, unsigned long y) __reentrant __sdcccall(4)
{
  unsigned long a, b, swap;
  unsigned int sign, opposite;
  unsigned int ea, eb, distance;

  a = x & 0x7fffffffUL;
  b = y & 0x7fffffffUL;
  if (a > 0x7f800000UL) return x | 0x00400000UL;
  if (b > 0x7f800000UL) return y | 0x00400000UL;
  if (a == 0x7f800000UL)
    {
      if (b == 0x7f800000UL && ((x ^ y) & 0x80000000UL))
        return 0x7fc00000UL;
      return x;
    }
  if (b == 0x7f800000UL) return y;
  /* 同号负零相加仍为负零；异号零和有限数精确相消返回正零。 */
  if (!a && !b) return (x & y) & 0x80000000UL;
  if (!a) return y;
  if (!b) return x;

  /* 特殊值已返回；后续只需要符号关系，不再保留两个原始位模式。
   * 交换幅值时按异号标记翻转结果符号，等价于选择较大幅值的符号。
   */
  sign = (unsigned int)(x >> 16) & 0x8000;
  opposite = (unsigned int)((x ^ y) >> 16) & 0x8000;
  /* 位模式的幅值顺序就是有限数大小，确保减法无无符号回绕。 */
  if (a < b)
    {
      swap = a; a = b; b = swap;
      sign ^= opposite;
    }
  ea = ((unsigned int)(a >> 16) >> 7);
  eb = ((unsigned int)(b >> 16) >> 7);
  a &= 0x007fffffUL;
  b &= 0x007fffffUL;
  if (ea) a |= 0x00800000UL; else ea = 1;
  if (eb) b |= 0x00800000UL; else eb = 1;
  a <<= 3;
  b <<= 3;
  distance = ea - eb;
  /* b 非零且最多27位；距离>=27时只剩sticky。其余循环最多26次。 */
  if (distance >= 27) b = 1;
  else while (distance)
    {
      b = (b >> 1) | (b & 1UL);
      --distance;
    }
  if (opposite)
    {
      a -= b;
      if (!a) return 0;
      /* 次正规数的有效指数为1，不能把隐藏位无条件补回去。 */
      while (a < 0x04000000UL && ea > 1) { a <<= 1; --ea; }
    }
  else
    {
      a += b;
      if (a & 0x08000000UL) { a = (a >> 1) | (a & 1UL); ++ea; }
    }
  if (ea >= 255) return ((unsigned long)sign << 16) | 0x7f800000UL;
  if (((unsigned int)a & 7) > 4UL || (((unsigned int)a & 7) == 4UL && ((unsigned int)a & 8))) a += 8;
  a >>= 3;
  /* 有效指数1覆盖次正规到正规边界；加法还覆盖舍入进位与上溢。 */
  return ((unsigned long)sign << 16) | (((unsigned long)((ea - 1) << 7) << 16) + a);
}
