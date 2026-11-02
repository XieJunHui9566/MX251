/* R42：复用完整加法；先保留第二个NaN原始符号，再翻转有限数/Inf/零。
 * 避免旧 -((-x)+y) 把精确相消的正零再次翻成负零。
 */
unsigned long __fsadd_bits(unsigned long, unsigned long) __reentrant __sdcccall(4);
unsigned long __fssub_bits(unsigned long x, unsigned long y) __reentrant __sdcccall(4)
{
  if ((y & 0x7fffffffUL) > 0x7f800000UL && (x & 0x7fffffffUL) <= 0x7f800000UL)
    return y | 0x00400000UL;
  return __fsadd_bits(x, y ^ 0x80000000UL);
}
