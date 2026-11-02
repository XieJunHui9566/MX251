/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_WIDE_H
#define MCS251_WIDE_H
#include "SDCCBBlock.h"
extern int mcs251WideEnabled;
extern int mcs251WideOrdinaryEnabled;
extern int mcs251VolatileDataWideEnabled;
extern int mcs251ConstantRotateFusionEnabled;
extern int mcs251WordCompareImmediateEnabled;
extern int mcs251RangeNarrowMultiplyEnabled;
extern char *mcs251CalleeCostFile,*mcs251CalleeCostSha;
extern int mcs251ScratchConstantRetentionEnabled;
extern int mcs251IndexedFieldEnabled;
extern char *mcs251IndexedFieldCapability;
extern char *mcs251IndexedFieldContract;
extern int mcs251OrderedAsmEnabled;
extern unsigned char mcs251OrderedAsmBits[256];
/* 不支持/容量限制允许旧路径接管；内部错误已报告，禁止继续发射旧路径。
 * 代码与诊断分别暂存，只有完整成功才提交候选机器代码。 */
typedef enum { MCS251_WIDE_FALLBACK, MCS251_WIDE_ACCEPTED, MCS251_WIDE_ERROR } mcs251_wide_result;
mcs251_wide_result mcs251TryWideFunction (ebbIndex *);
#endif
