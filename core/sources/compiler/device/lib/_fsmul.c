/*-------------------------------------------------------------------------
   _fsmul.c - Floating point library in optimized assembly for 8051

   Copyright (c) 2004, Paul Stoffregen, paul@pjrc.com

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library; see the file COPYING. If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.

   As a special exception, if you link this library with other files,
   some of which are compiled with SDCC, to produce an executable,
   this library does not by itself cause the resulting executable to
   be covered by the GNU General Public License. This exception does
   not however invalidate any other reasons why the executable file
   might be covered by the GNU General Public License.
-------------------------------------------------------------------------*/


#define __SDCC_FLOAT_LIB
#include <float.h>


#ifdef FLOAT_ASM_MCS51

// float __fsmul (float a, float b) __reentrant
static void dummy(void) __naked
{
	__asm
	.globl	___fsmul
___fsmul:
	// extract the two inputs, placing them into:
	//      sign     exponent   mantissa
	//      ----     --------   --------
	//  a:  sign_a   exp_a      r4/r3/r2
	//  b:  sign_b   exp_b      r7/r6/r5

	lcall	fsgetargs

	// first check if either input is zero
	cjne	r4, #0, 00002$
00001$:
	ljmp	fs_return_zero

00002$:
	mov	a, r7
	jz	00001$

	// compute final sign bit
	jnb	sign_b, 00003$
	cpl	sign_a
00003$:

	// check if either input is infinity
	mov	a, exp_b
	cjne	a, #0xFF, 00004$
	ljmp	fs_return_inf
00004$:
	mov	a, exp_a
	cjne	a, #0xFF, 00005$
	ljmp	fs_return_inf
00005$:

	// add the exponents
	add	a, exp_b
	// if carry then no underflow
	jc	00006$
	add	a, #130
	jc	00007$
	ljmp	fs_return_zero

00006$:
	add	a, #131
	dec	a
	jnc	00007$
	ljmp	fs_return_inf

00007$:
	mov	exp_a, a

	// now we need to multiply r4/r3/r2 * r7/r6/r5
	// ------------------------------------------
	//	                        r2 * r5		<< 0
	//	            r3 * r5  +  r2 * r6		<< 8
	//	r4 * r5  +  r3 * r6  +  r2 * r7		<< 16
	//	r4 * r6  +  r3 * r7			<< 24
	//	r4 * r7					<< 32
	//
	// This adds quite a bit of code, but it is a LOT faster
	// than three calls to __mululong...

	// output goes into r4/r3/r2/r1/r0/xx

	mov	a, r2
	mov	b, r5
	mul	ab			// r2 * r5
	// discard lowest 8 bits
	mov	r0, b
	// range 0-FE

	mov	a, r2
	mov	b, r6
	mul	ab			// r2 * r6
	add	a, r0
	mov	r0, a
	clr	a
	addc	a, b
	mov	r1, a
	// range 0-FEFF

	mov	a, r3
	mov	b, r5
	mul	ab			// r3 * r5
	add	a, r0
	// discard lowest 8 bits
	mov	a, r1
	addc	a, b
	mov	r1, a
	clr	a
	rlc	a
	xch	a, r2
	// range 0-1FD

	mov	b, r7
	mul	ab			// r2 * r7
	add	a, r1
	mov	r1, a
	mov	a, r2
	addc	a, b
	mov	r2, a
	// range 0-FFFE

	mov	a, r3
	mov	r0, a
	mov	b, r6
	mul	ab			// r3 * r6
	add	a, r1
	mov	r1, a
	mov	a, r2
	addc	a, b
	mov	r2, a
	clr	a
	rlc	a
	mov	r3, a
	// range 0-1FDFF

	mov	a, r4
	mov	b, r5
	mul	ab			// r4 * r5
	add	a, r1
	mov	r1, a
	mov	a, r2
	addc	a, b
	mov	r2, a
	clr	a
	addc	a, r3
	mov	r3, a
	// range 0-2FC00

	mov	a, r0 // r3
	mov	b, r7
	mul	ab			// r3 * r7
	add	a, r2
	mov	r2, a
	mov	a, r3
	addc	a, b
	mov	r3, a
	clr	a
	rlc	a
	xch	a, r4
	// range 0-100FD00

	mov	r5, a
	mov	b, r6
	mul	ab			// r4 * r6
	add	a, r2
	mov	r2, a
	mov	a, r3
	addc	a, b
	mov	r3, a
	clr	a
	addc	a, r4
	mov	r4, a
	// range 0-1FEFE00

	mov	a, r5 // r4
	mov	b, r7
	mul	ab			// r4 * r7
	add	a, r3
	mov	r3, a
	mov	a, r4
	addc	a, b
	mov	r4, a
	// range 40000000-FFFFFE00

	jb	acc.7, 00010$
	lcall	fs_normalize_a

00010$:
	ljmp	fs_round_and_return
	__endasm;
}

#elif defined(__SDCC_mcs251)

/* 原生入口只接收正规输入且 ea+eb-127 在 [1,253] 的情况。
 * 其余输入尾跳原 C 实现，不删减 NaN、次正规数或上下溢出处理。
 * A/B/DPH/DPL 为首参及返回值；第二参数沿用各模型 ABI。
 * R0..R15 为 caller-save；快路径额外最多压栈 5 字节，无静态工作区。
 */
#if defined(__SDCC) && !defined(_SDCC_NO_ASM_LIB_FUNCS)
static void __fsmul_native_entry (void) __naked
{
  __asm
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
#include "mcs251/native-float/fsmul_fast.inc"
#else
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    .globl ___m251r4___fsmul
    .globl ___m251r4___fsmul_fallback
___m251r4___fsmul:
    mov dr0,dr4
    mov dr4,dr12
#else
    .globl ___fsmul
    .globl ___fsmul_fallback
#if !defined(__SDCC_STACK_AUTO)
    .globl ___fsmul_PARM_2
    .globl ___fsmul_fallback_PARM_2
    ___fsmul_PARM_2 = ___fsmul_fallback_PARM_2
#endif
___fsmul:
    mov r0,a
    mov r1,b
    mov r2,dph
    mov r3,dpl
#if defined(__SDCC_STACK_AUTO)
    mov a,@spx-6
    mov r4,a
    mov a,@spx-5
    mov r5,a
    mov a,@spx-4
    mov r6,a
    mov a,@spx-3
    mov r7,a
#elif defined(__SDCC_MODEL_SMALL)
    mov r4,___fsmul_fallback_PARM_2
    mov r5,(___fsmul_fallback_PARM_2+1)
    mov r6,(___fsmul_fallback_PARM_2+2)
    mov r7,(___fsmul_fallback_PARM_2+3)
#else
    mov dptr,#___fsmul_fallback_PARM_2
    mov dpxl,#(___fsmul_fallback_PARM_2 >> 16)
    mov a,@dpx
    mov r4,a
    inc dpx
    mov a,@dpx
    mov r5,a
    inc dpx
    mov a,@dpx
    mov r6,a
    inc dpx
    mov a,@dpx
    mov r7,a
#endif
#endif
    mov a,r1
    mov c,acc.7
    mov a,r0
    rlc a
    jz 00191$
    cjne a,#255,00181$
    sjmp 00191$
00181$:
    mov r9,a
    mov a,r5
    mov c,acc.7
    mov a,r4
    rlc a
    jz 00191$
    cjne a,#255,00182$
    sjmp 00191$
00182$:
    add a,r9
    mov r9,a
    clr a
    rlc a
    mov r8,a
    mov a,r9
    clr c
    subb a,#127
    mov r9,a
    mov a,r8
    subb a,#0
    jnz 00191$
    mov a,r9
    jz 00191$
    cjne a,#254,00183$
    sjmp 00191$
00183$:
    jnc 00191$
    sjmp 00192$
00191$:
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    mov dr12,dr4
    mov dr4,dr0
    ejmp ___m251r4___fsmul_fallback
#else
    mov a,r0
    mov b,r1
    mov dph,r2
    mov dpl,r3
    ejmp ___fsmul_fallback
#endif
00192$:
    mov a,r0
    xrl a,r4
    anl a,#0x80
    push acc
    mov a,r1
    orl a,#0x80
    mov r0,a
    mov r1,r2
    mov r2,r3
    mov a,r5
    orl a,#0x80
    mov r4,a
    mov r5,r6
    mov r6,r7
    ; ah*bh 保存于栈；源 WR4 与结果 DR12 不重叠。
    mov r12,r0
    mov r13,r1
    mul wr12,wr4
    push r12
    push r13
    push r14
    push r15
    mov a,r2
    mov b,r6
    mul ab
    mov r3,a
    mov r7,b
    movz wr12,r2
    mul wr12,wr4
    mov r4,r13
    mov r5,r14
    mov r2,r15
    movz wr12,r6
    mul wr12,wr0
    mov a,r15
    add a,r2
    mov r15,a
    mov a,r14
    addc a,r5
    mov r14,a
    mov a,r13
    addc a,r4
    mov r13,a
    mov a,r12
    addc a,#0
    mov r12,a
    mov a,r15
    add a,r7
    mov r15,a
    mov a,r14
    addc a,#0
    mov r14,a
    mov a,r13
    addc a,#0
    mov r13,a
    mov a,r12
    addc a,#0
    mov r12,a
    ; 最低 16 位只剩 sticky 用途，但此前的部分乘积进位已完整保留。
    mov a,r15
    orl a,r3
    mov r7,a
    pop r3
    pop r2
    pop r1
    pop r0
    mov r6,r14
    mov r5,r13
    mov r4,r12
    mov a,r3
    add a,r6
    mov r3,a
    mov a,r2
    addc a,r5
    mov r2,a
    mov a,r1
    addc a,r4
    mov r1,a
    mov a,r0
    addc a,#0
    mov r0,a
    jnb acc.7,00193$
    mov a,r3
    anl a,#1
    orl a,r7
    mov r7,a
    clr c
    mov a,r0
    rrc a
    mov r0,a
    mov a,r1
    rrc a
    mov r1,a
    mov a,r2
    rrc a
    mov r2,a
    mov a,r3
    rrc a
    mov r3,a
    inc r9
00193$:
    mov a,r3
    anl a,#0x7f
    cjne a,#0x40,00194$
    mov a,r3
    anl a,#0x80
    orl a,r7
    jz 00196$
    sjmp 00195$
00194$:
    jc 00196$
00195$:
    mov a,r3
    add a,#0x80
    mov r3,a
    mov a,r2
    addc a,#0
    mov r2,a
    mov a,r1
    addc a,#0
    mov r1,a
    mov a,r0
    addc a,#0
    mov r0,a
00196$:
    ; hi >> 7，依次读取原字节后覆盖低字节，避免额外四字节副本。
    mov a,r3
    mov c,acc.7
    mov a,r2
    rlc a
    mov r3,a
    mov a,r1
    rlc a
    mov r2,a
    mov a,r0
    rlc a
    mov r1,a
    clr a
    rlc a
    mov r0,a
    mov a,r9
    dec a
    clr c
    rrc a
    mov r12,a
    clr a
    rrc a
    add a,r1
    mov r1,a
    mov a,r12
    addc a,r0
    mov r0,a
    pop acc
    orl a,r0
#if defined(__SDCC_MCS251_NATIVE_RUNTIME)
    mov r4,a
    mov r5,r1
    mov r6,r2
    mov r7,r3
#else
    mov b,r1
    mov dph,r2
    mov dpl,r3
#endif
    eret
#endif
  __endasm;
}
#define __fsmul __fsmul_fallback
#endif

/* 24x24 位精确乘积拆成 32+16 位，不调用 64 位运行库。
 * 只在最终精度处做一次最近偶数舍入，次正规输出也不先舍入成正规数。
 * 与本端口除法一致：保留首个 NaN 的 payload 并 quiet；无 fenv 标志。
 */
float __fsmul (float a1, float a2) __SDCC_FLOAT_BINARY_ABI
{
  union { float f; unsigned long u; } x, y;
  unsigned long a, b, hi, cross;
  unsigned short lo;
  unsigned char sign;
  int ea, eb, exp;

  x.f = a1; y.f = a2;
  a = x.u; b = y.u;
  sign = (unsigned char)((a ^ b) >> 24) & 0x80;
  ea = (int)((a >> 23) & 255); eb = (int)((b >> 23) & 255);
  /* 正规数仅检查指数；payload/零判断只留在特殊路径。 */
  if (ea == 255 || eb == 255)
    {
      if (ea == 255 && (a & 0x007fffffUL)) { x.u = a | 0x00400000UL; return x.f; }
      if (eb == 255 && (b & 0x007fffffUL)) { y.u = b | 0x00400000UL; return y.f; }
      x.u = (!(a & 0x7fffffffUL) || !(b & 0x7fffffffUL)) ?
        0x7fc00000UL : ((unsigned long)sign << 24) | 0x7f800000UL;
      return x.f;
    }
  a &= 0x007fffffUL; b &= 0x007fffffUL;
  if (ea) a |= 0x00800000UL;
  else
    {
      if (!a) { x.u = (unsigned long)sign << 24; return x.f; }
      ea = 1; do { a <<= 1; --ea; } while (!(a & 0x00800000UL));
    }
  if (eb) b |= 0x00800000UL;
  else
    {
      if (!b) { x.u = (unsigned long)sign << 24; return x.f; }
      eb = 1; do { b <<= 1; --eb; } while (!(b & 0x00800000UL));
    }
  exp = ea + eb - 127;

  /* a=256*ah+al。四个 16x16 乘积均精确，cross 最大 25 位。
   * 低 16 位只在最终舍入时作为 sticky，但形成 hi 时不能丢进位。
   */
  hi = (unsigned long)(unsigned short)(a >> 8) * (unsigned short)(b >> 8);
  lo = (unsigned short)(unsigned char)a * (unsigned char)b;
  cross = (unsigned long)(unsigned short)(a & 0xffUL) * (unsigned short)(b >> 8);
  cross += (unsigned long)(unsigned short)(b & 0xffUL) * (unsigned short)(a >> 8);
  cross += lo >> 8;
  lo = (unsigned short)(cross << 8) | (unsigned char)lo;
  hi += cross >> 8;

  if (hi & 0x80000000UL) { lo |= (unsigned short)(hi & 1UL); hi >>= 1; ++exp; }
  if (exp >= 255) { x.u = ((unsigned long)sign << 24) | 0x7f800000UL; return x.f; }
  if (exp <= 0)
    {
      if (exp < -23) { x.u = (unsigned long)sign << 24; return x.f; }
      /* 最多 24 步；被移出的任何非零位都保留在 sticky 中。 */
      do { lo |= (unsigned short)(hi & 1UL); hi >>= 1; ++exp; } while (exp <= 0);
      exp = 0;
    }
  if ((hi & 0x7fUL) > 0x40UL ||
      ((hi & 0x7fUL) == 0x40UL && ((hi & 0x80UL) || lo))) hi += 0x80UL;
  hi >>= 7;
  /* 正规尾数含隐藏位：直接相加也覆盖进位到下一指数/无穷大的情况。 */
  x.u = ((unsigned long)sign << 24) | (exp ? ((unsigned long)(exp - 1) << 23) + hi : hi);
  return x.f;
}

#if defined(__SDCC) && !defined(_SDCC_NO_ASM_LIB_FUNCS)
#undef __fsmul
#endif

#else

/*
** libgcc support for software floating point.
** Copyright (C) 1991 by Pipeline Associates, Inc.  All rights reserved.
** Permission is granted to do *anything* you want with this file,
** commercial or otherwise, provided this message remains intact.  So there!
** I would appreciate receiving any updates/patches/changes that anyone
** makes, and am willing to be the repository for said changes (am I
** making a big mistake?).
**
** Pat Wood
** Pipeline Associates, Inc.
** pipeline!phw@motown.com or
** sun!pipeline!phw or
** uunet!motown!pipeline!phw
*/

/* (c)2000/2001: hacked a little by johan.knol@iduna.nl for sdcc */

union float_long
  {
    float f;
    unsigned long l;
  };

/* multiply two floats */
float __fsmul (float a1, float a2) __SDCC_FLOAT_BINARY_ABI
{
  volatile union float_long fl1, fl2;
  unsigned long result;
  int exp;
  char sign;

  fl1.f = a1;
  fl2.f = a2;

  if (!fl1.l || !fl2.l)
    return (0);

  /* compute sign and exponent */
  sign = SIGN (fl1.l) ^ SIGN (fl2.l);
  exp = EXP (fl1.l) - EXCESS;
  exp += EXP (fl2.l);

  fl1.l = MANT (fl1.l);
  fl2.l = MANT (fl2.l);

  /* the multiply is done as one 16x16 multiply and two 16x8 multiplies */
  result = (unsigned long)((unsigned short)(fl1.l >> 8)) * (unsigned short)(fl2.l >> 8);
  result += ((unsigned long)((unsigned short)(fl1.l & 0xff)) * (unsigned short)(fl2.l >> 8)) >> 8;
  result += ((unsigned long)((unsigned short)(fl2.l & 0xff)) * (unsigned short)(fl1.l >> 8)) >> 8;

  /* round, phase 1 */
  result += 0x40;

  if (result & SIGNBIT)
    {
      /* round, phase 2 */
      result += 0x40;
      result >>= 8;
    }
  else
    {
      result >>= 7;
      exp--;
    }

  result &= ~HIDDEN;

  /* pack up and go home */
  if (exp >= 0x100)
    fl1.l = (sign ? SIGNBIT : 0) | __INFINITY;
  else if (exp < 0)
    fl1.l = 0;
  else
    fl1.l = PACK (sign ? SIGNBIT : 0 , exp, result);
  return (fl1.f);
}

#endif
