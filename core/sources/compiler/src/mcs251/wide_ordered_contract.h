/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_WIDE_ORDERED_CONTRACT_H
#define MCS251_WIDE_ORDERED_CONTRACT_H
#include <string.h>
#include <ctype.h>
#include "target_sfr.h"
typedef struct { unsigned char operation,address,bit; } wide_asm_instruction;
static inline int wide_asm_core_address(unsigned address);

static inline int
wide_asm_hex(char c)
{
  if(c>='0' && c<='9')return c-'0';
  if(c>='a' && c<='f')return c-'a'+10;
  if(c>='A' && c<='F')return c-'A'+10;
  return -1;
}

static inline int
wide_asm_allocator_bits(unsigned address,unsigned mask)
{
  return wide_asm_core_address(address) || (address==0x87 && (mask&0x3f));
}

static inline int
wide_asm_core_bits(unsigned address,unsigned mask)
{
  /* Mapping controls require the separate storage-stable memory-state contract. */
  return wide_asm_allocator_bits(address,mask) ||
    (address==0x8e && (mask&0x02)) || (address==0xba && (mask&0x80));
}

/* Packing a complete ISR context does not move body instructions or discard
 * state. PCON bit writes do not inspect the private stack or alias GPRs, even
 * when they affect execution state. Keep that fact separate from permission
 * to move accesses or retain cached values across those writes. */
static inline int
wide_asm_private_frame_bits(unsigned address,unsigned mask)
{
  return address==0x87 || !wide_asm_core_bits(address,mask);
}

static inline int
wide_asm_core_address(unsigned address)
{
  /* SP/DPTR 扩展、PSW/PSW1、ACC/B 均不得通过目标声明放开。
   * PCON 的串口控制位仍须显式逐位声明，低六位由 core_bits 硬拒绝。
   * peripheral-v1 使用同一核心状态分类；特殊映射位仍须独立声明。
   */
  return address<0x80 || (address>=0x81 && address<=0x86) ||
    address==0xa2 || mcs251StackSfrAddress(address) || address==0xd0 || address==0xd1 ||
    address==0xe0 || address==0xe3 || address==0xf0;
}

/* 一次性验证完整目标声明；失败不能留下部分生效的能力。调用方
 * 只在返回 NULL 时发布输出，并把原声明放入构建命令和内容指纹。
 */
static inline const char *
wide_asm_parse_bit_contract(const char *bits,unsigned char output[256],int memoryState)
{
  unsigned char masks[256]={0};unsigned count=0;
  if(!bits || !*bits || strlen(bits)>767)return "expected non-empty NN:MM comma-separated bit capabilities";
  for(const char *p=bits;*p;) {
    if(strlen(p)<5 || wide_asm_hex(p[0])<0 || wide_asm_hex(p[1])<0 ||
       p[2]!=':' || wide_asm_hex(p[3])<0 || wide_asm_hex(p[4])<0)
      return "expected two hex digits for each SFR byte and mask (NN:MM)";
    unsigned address=(unsigned)(16*wide_asm_hex(p[0])+wide_asm_hex(p[1]));
    unsigned mask=(unsigned)(16*wide_asm_hex(p[3])+wide_asm_hex(p[4]));
    if(++count>128 || !mask || masks[address] ||
       (memoryState?wide_asm_allocator_bits(address,mask):wide_asm_core_bits(address,mask)))
      return "zero/duplicate mask, core-state address or capability count is invalid";
    masks[address]=(unsigned char)mask;p+=5;
    if(!*p)break;
    if(*p++!=',' || !*p)return "expected comma separator with no empty capability";
  }
  memcpy(output,masks,sizeof masks);return NULL;
}

static inline const char *
wide_asm_parse_capabilities(const char *bits,unsigned char output[256])
{
  /* Architecture contract: ordinary direct SFR bit writes leave the register
   * bank, stack and compiler storage accessible. SETB/CLR/CPL do not touch
   * GPRs or flags; MIR still treats every block as a full memory/motion barrier.
   * Numeric masks retain their original, explicitly restricted semantics. */
  if(bits && !strcmp(bits,"peripheral-v1")) {
    memset(output,0,256);
    for(unsigned address=0x80;address<256;++address)
      for(unsigned bit=0;bit<8;++bit)
        if(!wide_asm_core_bits(address,1u<<bit))output[address]|=1u<<bit;
    return NULL;
  }
  return wide_asm_parse_bit_contract(bits,output,0);
}

/* Shared by MIR admission and machine-pipeline admission. Empty/comment
 * lines have operation zero. Numeric byte.bit retains the exact SFR byte. */
static inline const char *
wide_asm_decode(const char *p,const char *end,wide_asm_instruction *instruction)
{
  memset(instruction,0,sizeof *instruction);
  while(p<end && (*p==' ' || *p=='\t'))++p;
  if(p==end || *p==';')return NULL;
  char op[5];unsigned length=0;
  while(p<end && isalpha((unsigned char)*p)) {
    if(length==sizeof op-1)return "unsupported-ordered-asm-opcode";
    op[length++]=(char)tolower((unsigned char)*p++);
  }
  op[length]=0;
  unsigned operation=!strcmp(op,"nop")?1:!strcmp(op,"setb")?2:!strcmp(op,"clr")?3:!strcmp(op,"cpl")?4:0;
  if(!operation || (p<end && *p!=' ' && *p!='\t' && *p!=';'))return "unsupported-ordered-asm-opcode-or-label";
  while(p<end && (*p==' ' || *p=='\t'))++p;
  if(operation!=1) {
    if(end-p<6 || p[0]!='0' || (p[1]!='x' && p[1]!='X') ||
       wide_asm_hex(p[2])<0 || wide_asm_hex(p[3])<0 || p[4]!='.' || p[5]<'0' || p[5]>'7')
      return "unsupported-ordered-asm-bit-spelling";
    instruction->address=(unsigned char)(16*wide_asm_hex(p[2])+wide_asm_hex(p[3]));
    instruction->bit=(unsigned char)(p[5]-'0');p+=6;
    while(p<end && (*p==' ' || *p=='\t'))++p;
  }
  if(p<end && *p!=';')return "unsupported-ordered-asm-trailing-text";
  instruction->operation=(unsigned char)operation;return NULL;
}

static inline const char *
wide_asm_validate(wide_asm_instruction instruction,const unsigned char bits[256],
                  const unsigned char memoryBits[256])
{
  if(instruction.operation<=1)return NULL;
  unsigned address=instruction.address,mask=1u<<instruction.bit;
  if(memoryBits[address]&mask)return NULL;
  if(wide_asm_core_bits(address,mask))return "ordered-asm-core-state-write";
  return bits[address]&mask?NULL:"ordered-asm-bit-not-capable";
}
#endif
