#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../call_save_plan.h"

int main (void)
{
  unsigned valid = 0, rejected = 0, executions = 0, wide = 0;
  for (unsigned mask = 0; mask < 65536; ++mask)
    {
      mcs251_call_save_plan plan;
      if (!mcs251BuildCallSavePlan (mask, &plan))
        { assert (mask & 0x0c00); ++rejected; continue; }
      ++valid;
      unsigned covered = 0, size = 0;
      assert (plan.count <= 14);
      for (unsigned i = 0; i < plan.count; ++i)
        {
          mcs251_call_save_step s = plan.steps[i];
          unsigned bits = ((1u << s.width) - 1) << s.first;
          assert ((s.width == 1 || s.width == 2 || s.width == 4) && !(s.first % s.width));
          assert (!(covered & bits) && !(bits & ~mask));
          covered |= bits; size += s.width;
        }
      assert (covered == mask && size == plan.bytes);
      wide += plan.wide != 0;
      for (unsigned seed = 0; seed < 4; ++seed)
        {
          unsigned char regs[16], before[16], memory[256], prefix[32];
          for (unsigned i = 0; i < 16; ++i) before[i] = regs[i] = (mask * 19 + i * 37 + seed * 71) & 255;
          for (unsigned i = 0; i < 256; ++i) memory[i] = (i * 23 + seed) & 255;
          memcpy (prefix, memory, sizeof prefix);
          unsigned sp = 31;
          /* 原生宽位 PUSH 的字节排列；私有保存区内容不与旧顺序混用。 */
          for (unsigned i = 0; i < plan.count; ++i)
            for (unsigned j = 0; j < plan.steps[i].width; ++j)
              memory[++sp] = regs[plan.steps[i].first + j];
          assert (sp == 31 + size);
          /* 栈参数/返回地址/嵌套现场位于保存区之上；不得覆盖调用者内容。 */
          for (unsigned i = 0; i < 24; ++i) memory[++sp] = (i * 13 + seed) & 255;
          for (unsigned i = 24; i-- > 0; ) assert (memory[sp--] == ((i * 13 + seed) & 255));
          memset (regs, 0xa7, sizeof regs);
          for (unsigned i = plan.count; i-- > 0; )
            {
              mcs251_call_save_step s = plan.steps[i];
              unsigned start = sp - s.width + 1;
              for (unsigned j = 0; j < s.width; ++j) regs[s.first + j] = memory[start + j];
              sp -= s.width;
            }
          assert (sp == 31 && !memcmp (prefix, memory, sizeof prefix));
          for (unsigned i = 0; i < 16; ++i) assert (regs[i] == ((mask & (1u << i)) ? before[i] : 0xa7));
          ++executions;
        }
    }
  mcs251_call_save_plan invalid;
  assert (!mcs251BuildCallSavePlan (0x10000, &invalid));
  printf ("{\"validMasks\":%u,\"rejectedMasks\":%u,\"roundTrips\":%u,\"masksWithWideGroups\":%u,\"passed\":true}\n",
          valid, rejected, executions, wide);
  return 0;
}
