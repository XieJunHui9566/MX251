/* 静态局部初始化是GSINIT片段，不是其所属函数的另一份函数体。
 * ABI2/ABI4都须保留初始化，同时只生成一次真实函数入口。 */
typedef unsigned int u16;
typedef unsigned long u32;
#ifdef NATIVE
#define API __sdcccall(4)
#else
#define API __reentrant
#endif
u16 counter(void) API
{
  static u16 count = 0x1234U;
  return count++;
}
u32 two_scopes(u16 choose) API
{
  if (choose)
    {
      static u32 value = 0x12345678UL;
      return ++value;
    }
  else
    {
      static u32 value = 0x87654321UL;
      return --value;
    }
}
void main(void)
{
  volatile u32 result;
  result = counter();
  result += two_scopes(1);
  for (;;) {}
}
