/* SPDX-License-Identifier: GPL-2.0-or-later
 * 仅把完整验证过的无寄存器破坏指令组导入有序 MIR；不透传任意汇编。
 * 位能力来自调用编译器的显式目标契约，而不是器件/函数/SDK 名称。
 * 契约保证这些 SFR 位不改变寄存器视图、存储映射、栈或执行模式。
 * 未声明、格式错误及核心状态一律整函数回退，不能部分接受一个块。
 */
#include "wide_ordered_contract.h"

static bool
wide_ordered_asm_profile(wide_function *f)
{
  f->orderedAsmEnabled=mcs251OrderedAsmEnabled!=0;
  memcpy(f->orderedAsmBits,mcs251OrderedAsmBits,sizeof f->orderedAsmBits);
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
    const char *p=line;while(p<end && (*p==' ' || *p=='\t'))++p;
    if(p<end && *p!=';') {
      char op[5];unsigned length=0;
      while(p<end && isalpha((unsigned char)*p)) {
        if(length==sizeof op-1)return wide_reject(f,"unsupported-ordered-asm-opcode",__LINE__);
        op[length++]=(char)tolower((unsigned char)*p++);
      }
      op[length]=0;
      unsigned operation=!strcmp(op,"nop")?1:!strcmp(op,"setb")?2:!strcmp(op,"clr")?3:0;
      if(!operation || (p<end && *p!=' ' && *p!='\t' && *p!=';'))
        return wide_reject(f,"unsupported-ordered-asm-opcode-or-label",__LINE__);
      while(p<end && (*p==' ' || *p=='\t'))++p;
      unsigned address=0,bit=0;
      if(operation!=1) {
        /* 只接受规范的数值 byte.bit；不猜别名、表达式或 classic 位编号。 */
        if(end-p<6 || p[0]!='0' || (p[1]!='x' && p[1]!='X') ||
           wide_asm_hex(p[2])<0 || wide_asm_hex(p[3])<0 || p[4]!='.' || p[5]<'0' || p[5]>'7')
          return wide_reject(f,"unsupported-ordered-asm-bit-spelling",__LINE__);
        address=(unsigned)(16*wide_asm_hex(p[2])+wide_asm_hex(p[3]));bit=(unsigned)(p[5]-'0');p+=6;
        if(wide_asm_core_bits(address,1u<<bit))return wide_reject(f,"ordered-asm-core-state-write",__LINE__);
        if(!(f->orderedAsmBits[address]&(1u<<bit)))return wide_reject(f,"ordered-asm-bit-not-capable",__LINE__);
        while(p<end && (*p==' ' || *p=='\t'))++p;
      }
      if(p<end && *p!=';')return wide_reject(f,"unsupported-ordered-asm-trailing-text",__LINE__);
      if(n->orderedAsmCount==WASM_MAX)return wide_reject(f,"ordered-asm-block-capacity",__LINE__);
      n->orderedAsm[n->orderedAsmCount++]=(wide_asm_instruction){operation,address,bit};
    }
    line=end;if(*line=='\r')++line;if(*line=='\n' || (unsigned char)*line==0x87)++line;
  }
  if(!n->orderedAsmCount)return wide_reject(f,"empty-ordered-asm",__LINE__);
  n->op=WM_ORDERED_ASM;n->byteAccess=true;
  dbuf_printf(f->diagnostics,"; wide ordered-asm admitted icode=%d instructions=%u barrier=memory-and-motion register-clobber=none\n",
              ic->key,n->orderedAsmCount);
  return true;
}
