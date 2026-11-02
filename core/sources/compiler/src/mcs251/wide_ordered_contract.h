/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MCS251_WIDE_ORDERED_CONTRACT_H
#define MCS251_WIDE_ORDERED_CONTRACT_H
#include <string.h>
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
wide_asm_core_bits(unsigned address,unsigned mask)
{
  /* PCON 低六位含电源/执行控制；EXTRAM 与 EAXFR 改变普通访存含义，误配能力也不能开放。
   * 同一 SFR 的其余位仍逐位核验，不能连带否定已声明的定时器位。 */
  return wide_asm_core_address(address) ||
    (address==0x87 && (mask&0x3f)) ||
    (address==0x8e && (mask&0x02)) || (address==0xba && (mask&0x80));
}

static inline int
wide_asm_core_address(unsigned address)
{
  /* SP/DPTR 扩展、PSW/PSW1、ACC/B 均不得通过目标声明放开。
   * PCON 的串口控制位仍须显式逐位声明，低六位由 core_bits 硬拒绝。
   * 其余地址也不是默认安全：仍须逐位命中独立核验的目标能力掩码。
   */
  return address<0x80 || (address>=0x81 && address<=0x86) ||
    address==0xa2 || address==0xd0 || address==0xd1 ||
    address==0xe0 || address==0xe3 || address==0xf0;
}

/* 一次性验证完整目标声明；失败不能留下部分生效的能力。调用方
 * 只在返回 NULL 时发布输出，并把原声明放入构建命令和内容指纹。
 */
static inline const char *
wide_asm_parse_capabilities(const char *bits,unsigned char output[256])
{
  unsigned char masks[256]={0};unsigned count=0;
  if(!bits || !*bits || strlen(bits)>767)return "expected non-empty NN:MM comma-separated bit capabilities";
  for(const char *p=bits;*p;) {
    if(strlen(p)<5 || wide_asm_hex(p[0])<0 || wide_asm_hex(p[1])<0 ||
       p[2]!=':' || wide_asm_hex(p[3])<0 || wide_asm_hex(p[4])<0)
      return "expected two hex digits for each SFR byte and mask (NN:MM)";
    unsigned address=(unsigned)(16*wide_asm_hex(p[0])+wide_asm_hex(p[1]));
    unsigned mask=(unsigned)(16*wide_asm_hex(p[3])+wide_asm_hex(p[4]));
    if(++count>128 || !mask || wide_asm_core_bits(address,mask) || masks[address])
      return "zero/duplicate mask, core-state address or capability count is invalid";
    masks[address]=(unsigned char)mask;p+=5;
    if(!*p)break;
    if(*p++!=',' || !*p)return "expected comma separator with no empty capability";
  }
  memcpy(output,masks,sizeof masks);return NULL;
}
#endif
