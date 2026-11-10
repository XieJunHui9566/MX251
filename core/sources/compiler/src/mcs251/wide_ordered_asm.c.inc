/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅把完整验证过的无寄存器破坏指令组导入有序 MIR；不透传任意汇编。
 * 位能力来自调用编译器的显式目标契约，而不是器件/函数/SDK 名称。
 * Ordinary bits leave memory mapping unchanged. Memory-state bits may alter
 * peripheral mapping, but preserve GPRs, flags, stack and compiler storage.
 * Both use the existing full memory/motion barrier throughout the MIR passes.
 */
#include "wide_ordered_contract.h"

static bool
wide_ordered_asm_profile(wide_function *f)
{
  f->orderedAsmEnabled=mcs251OrderedAsmEnabled!=0;
  memcpy(f->orderedAsmBits,mcs251OrderedAsmBits,sizeof f->orderedAsmBits);
  memcpy(f->memoryStateBits,mcs251MemoryStateBits,sizeof f->memoryStateBits);
  return true;
}

static bool
wide_ordered_asm_import(wide_function *f,wide_node *n,iCode *ic)
{
  if(!f->orderedAsmEnabled)return wide_reject(f,"ordered-asm-disabled",__LINE__);
  const char *source=IC_INLINE(ic);
  if(!source)return wide_reject(f,"empty-ordered-asm",__LINE__);
  for(const char *line=source;*line;) {
    /* SDCC 预处理后的块汇编以 0x87 保存部分换行；与 genInline 的
     * 分行规则一致，仅恢复指令边界，不放行任何额外操作或尾随文本。 */
    const char *end=line;while(*end && *end!='\n' && *end!='\r' && (unsigned char)*end!=0x87)++end;
    wide_asm_instruction instruction;
    const char *error=wide_asm_decode(line,end,&instruction);
    if(!error)error=wide_asm_validate(instruction,f->orderedAsmBits,f->memoryStateBits);
    if(error)return wide_reject(f,error,__LINE__);
    if(instruction.operation) {
      if(n->orderedAsmCount==WASM_MAX)return wide_reject(f,"ordered-asm-block-capacity",__LINE__);
      n->orderedAsm[n->orderedAsmCount++]=instruction;
      if(instruction.operation>1 && (f->memoryStateBits[instruction.address]&(1u<<instruction.bit)))
        n->memoryStateBarrier=true;
    }
    line=end;if(*line=='\r')++line;if(*line=='\n' || (unsigned char)*line==0x87)++line;
  }
  if(!n->orderedAsmCount)return wide_reject(f,"empty-ordered-asm",__LINE__);
  n->op=WM_ORDERED_ASM;n->byteAccess=true;
  dbuf_printf(f->diagnostics,"; wide ordered-asm admitted icode=%d instructions=%u barrier=memory-and-motion register-clobber=none memory-state=%u\n",
              ic->key,n->orderedAsmCount,n->memoryStateBarrier);
  return true;
}
