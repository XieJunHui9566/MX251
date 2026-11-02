/* MCS251 register transfer value tracking. GPL-2.0-or-later. */
#ifndef MCS251_TRANSFER_VALUES_H
#define MCS251_TRANSFER_VALUES_H
#include <ctype.h>
#include <string.h>
#include "register_facts.h"

typedef struct { unsigned char value[24]; } mcs251_transfer_values;
typedef struct { unsigned first, width; } mcs251_transfer_register;

static void
mcs251TransferReset (mcs251_transfer_values *state)
{
  for (unsigned i = 0; i < 24; ++i) state->value[i] = i;
}

static void
mcs251TransferSpace (const char **text)
{
  while (isspace ((unsigned char)**text)) ++*text;
}

/* 只识别确定的物理寄存器切片。A/B、SFR、ARn 和 SP/DPTR 不纳入模型；
 * R0..R7 字节访问可能受寄存器组选择影响，也作为边界。WR/DR 按大端
 * 物理字节位置重叠；超出 R15 的字节 MOV 没有对应编码。 */
static int
mcs251TransferRegister (const char **text, mcs251_transfer_register *reg)
{
  const char *p = *text;
  unsigned n = 0, digits = 0, width;
  mcs251TransferSpace (&p);
  if (*p == 'r') { width = 1; ++p; }
  else if (p[0] == 'w' && p[1] == 'r') { width = 2; p += 2; }
  else if (p[0] == 'd' && p[1] == 'r') { width = 4; p += 2; }
  else return 0;
  while (isdigit ((unsigned char)*p))
    {
      if (++digits > 2) return 0;
      n = 10 * n + (*p++ - '0');
    }
  if (!digits || !mcs251RegisterEncodable (n, width) || n + width > 24 ||
      (width == 1 && n < 8) || (mcs251RegisterBytes (n, width) & MCS251_AB_BYTES)) return 0;
  reg->first = n; reg->width = width; *text = p;
  return 1;
}

static int
mcs251TransferOperands (const char *text, mcs251_transfer_register *dest,
                        mcs251_transfer_register *source)
{
  if (!mcs251TransferRegister (&text, dest)) return 0;
  mcs251TransferSpace (&text);
  if (*text++ != ',') return 0;
  if (!mcs251TransferRegister (&text, source)) return 0;
  mcs251TransferSpace (&text);
  mcs251_register_effects effects;
  return dest->width == source->width && (!*text || *text == ';') &&
         mcs251RegisterMoveEffects (dest->first, source->first, dest->width, &effects);
}

static int
mcs251TransferParse (const char *text, mcs251_transfer_register *dest,
                     mcs251_transfer_register *source)
{
  mcs251TransferSpace (&text);
  return !strncmp (text, "mov", 3) && isspace ((unsigned char)text[3]) &&
         mcs251TransferOperands (text + 3, dest, source);
}

/* 新值标识只需区别于当前活跃的 24 个字节；从 48 个标识中选空闲项，
 * 无递增计数器溢出，也不误删仍与旧值相等的另一寄存器。 */
static void
mcs251TransferInvalidate (mcs251_transfer_values *state, unsigned first, unsigned width)
{
  for (unsigned i = 0; i < width; ++i)
    {
      unsigned fresh;
      for (fresh = 0; fresh < 48; ++fresh)
        {
          unsigned j;
          for (j = 0; j < 24 && state->value[j] != fresh; ++j) {}
          if (j == 24) break;
        }
      state->value[first + i] = fresh;
    }
}

/* 宽位寄存器操作的明确读写范围，供前向等价跟踪及反向活跃分析共用。 */
static int
mcs251TransferArithmetic (const char *text, mcs251_transfer_register *d,
                          mcs251_transfer_register *s)
{
  mcs251TransferSpace (&text);
  if ((!strncmp (text, "add", 3) || !strncmp (text, "sub", 3) ||
       !strncmp (text, "anl", 3) || !strncmp (text, "orl", 3) ||
       !strncmp (text, "xrl", 3) || !strncmp (text, "cmp", 3)) &&
      isspace ((unsigned char)text[3]) &&
      mcs251TransferOperands (text + 3, d, s) && d->width >= 2)
    return strncmp (text, "cmp", 3) ? 1 : 2;
  return 0;
}

#define MCS251_TRANSFER_ALL_LIVE 0xffffffu

/* 边界处所有寄存器都活跃。仅删除在任何读取之前被完整覆盖的 MOV；
 * 算术指令仍保留并计入其所有输入，避免丢失标志位依赖。 */
static int
mcs251TransferDead (unsigned *live, const char *text)
{
  mcs251_transfer_register d, s;
  int move = mcs251TransferParse (text, &d, &s);
  int arithmetic = move ? 0 : mcs251TransferArithmetic (text, &d, &s);
  if (!move && !arithmetic) { *live = MCS251_TRANSFER_ALL_LIVE; return 0; }
  unsigned dest = ((1u << d.width) - 1) << d.first;
  unsigned source = ((1u << s.width) - 1) << s.first;
  if (move && !(*live & dest)) return 1;
  if (move) *live = (*live & ~dest) | source;
  else *live |= dest | source;
  return 0;
}

/* 返回 1 仅表示 MOV 的每个目标字节已具有同一符号值。先快照源字节，
 * 再更新目标，避免 WR/DR/字节别名使顺序复制产生错误。未知指令清空知识。 */
static int
mcs251TransferApply (mcs251_transfer_values *state, const char *text)
{
  mcs251_transfer_register d, s;
  unsigned char snapshot[4];
  int redundant = 1;
  if (!mcs251TransferParse (text, &d, &s))
    {
      /* 只描述双寄存器的原生宽位操作；CMP 只改标志，其他操作只写目标。
       * 不解释内存、乘除法隐含结果、调用或任意未建模指令。 */
      int arithmetic = mcs251TransferArithmetic (text, &d, &s);
      if (arithmetic)
        {
          if (arithmetic == 1) mcs251TransferInvalidate (state, d.first, d.width);
          return 0;
        }
      mcs251TransferReset (state); return 0;
    }
  for (unsigned i = 0; i < d.width; ++i)
    {
      snapshot[i] = state->value[s.first + i];
      if (state->value[d.first + i] != snapshot[i]) redundant = 0;
    }
  for (unsigned i = 0; i < d.width; ++i) state->value[d.first + i] = snapshot[i];
  return redundant;
}
#endif
