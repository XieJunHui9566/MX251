/* Experimental closed-firmware return contract; public ABI stays unchanged. */
#ifndef MCS251_INTERNAL_RETURN_H
#define MCS251_INTERNAL_RETURN_H
#include "common.h"
bool mcs251InternalReturn (symbol *function);
bool mcs251InternalArgument (symbol *function);
/* 在寄存器参数标记前，按完整镜像证明派生的 opt-in 契约选择既有 ABI4。 */
void mcs251ConfigureInternalAbi (symbol *function, sym_link *functionType);
/* 只读查询已载入且签名相符的闭合原生契约；不改写声明。 */
bool mcs251InternalNative (symbol *function);
bool mcs251FlatAggregateToken (sym_link *type, char *text, size_t size);
bool mcs251InternalTransportMatches (symbol *function, const char *signature);
#endif
