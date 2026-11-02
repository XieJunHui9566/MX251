/* Shared MCS251 register facts. GPL-2.0-or-later. */
#ifndef MCS251_REGISTER_FACTS_H
#define MCS251_REGISTER_FACTS_H
#include <stdint.h>

enum { MCS251_BYTE_B = 10, MCS251_BYTE_A = 11 };
#define MCS251_AB_BYTES ((UINT32_C(1) << MCS251_BYTE_A) | (UINT32_C(1) << MCS251_BYTE_B))

/* 只描述通用物理字节区，不包括 DPX/SPX/SFR。物理切片存在不等于有字节编码。
 * 消费者仍负责寄存器组、暂存保留和 ABI 等准入限制；无效切片返回零。 */
static inline uint32_t
mcs251RegisterBytes (unsigned first, unsigned width)
{
  if ((width != 1 && width != 2 && width != 4) || first >= 32 ||
      first % width || width > 32 - first) return 0;
  return ((UINT32_C(1) << width) - 1) << first;
}

static inline int
mcs251RegisterEncodable (unsigned first, unsigned width)
{
  return mcs251RegisterBytes (first, width) != 0 && (width != 1 || first < 16);
}

typedef struct { uint32_t reads, writes; unsigned flags, memory; } mcs251_register_effects;

/* 当前只描述同宽 GPR MOV。其它指令副作用为未知，不能从空掩码推导安全。
 * A/R11 与 B/R10 使用同一物理字节编号；银行与 SFR 名称解析属于消费者。 */
static inline int
mcs251RegisterMoveEffects (unsigned dest, unsigned source, unsigned width,
                          mcs251_register_effects *effects)
{
  if (!mcs251RegisterEncodable (dest, width) ||
      !mcs251RegisterEncodable (source, width)) return 0;
  effects->reads = mcs251RegisterBytes (source, width);
  effects->writes = mcs251RegisterBytes (dest, width);
  effects->flags = effects->memory = 0;
  return 1;
}
#endif
