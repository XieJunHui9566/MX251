/*-------------------------------------------------------------------------
   _modulong.c - routine for modulus of 32 bit unsigned long

   Copyright (C) 1999, Sandeep Dutta . sandeep.dutta@usa.net
   Bug fixes by Martijn van Balen, aed@iae.nl

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

/*   Assembler-functions are provided for:
     mcs51 small
     mcs51 small stack-auto
*/

#include <sdcc-lib.h>

#if !defined(__SDCC_USE_XSTACK) && !defined(_SDCC_NO_ASM_LIB_FUNCS)
#  if defined(__SDCC_mcs51)
#    if defined(__SDCC_MODEL_SMALL)
#      if defined(__SDCC_STACK_AUTO) && !defined (__SDCC_PARMS_IN_BANK1)
#        define _MODULONG_ASM_SMALL_AUTO
#      else
#        define _MODULONG_ASM_SMALL
#      endif
#    endif
#  endif
#endif

#if defined _MODULONG_ASM_SMALL

static void
_modlong_dummy (void) __naked
{
	__asm

	.globl __modulong

	#define a0	dpl
	#define a1	dph
	#define a2	b
	#define a3	r1
	#define count   r0

__modulong:
#if defined(__SDCC_PARMS_IN_BANK1)
	#define b0      (b1_0)
	#define b1      (b1_1)
	#define b2      (b1_2)
	#define b3      (b1_3)
#else
#if defined(__SDCC_NOOVERLAY)
	.area DSEG    (DATA)
#else
	.area OSEG    (OVR,DATA)
#endif

	.globl __modulong_PARM_2
	.globl __modslong_PARM_2

__modulong_PARM_2:
__modslong_PARM_2:
	.ds	4

	.area CSEG    (CODE)

	#define b0      (__modulong_PARM_2)
	#define b1      (__modulong_PARM_2 + 1)
	#define b2      (__modulong_PARM_2 + 2)
	#define b3      (__modulong_PARM_2 + 3)
#endif
				; parameter a comes in a, b, dph, dpl
	mov	a3,a		; save parameter a3

	mov	a,b0		; b == 0? avoid endless loop
	orl	a,b1
	orl	a,b2
	orl	a,b3
	jz	div_by_0

	mov	count,#0
	clr	c		; when loop1 jumps immediately to loop2

loop1:	inc	count

	mov	a,b3		; if (!MSB_SET(b))
	jb	acc.7,loop2

	mov	a,b0		; b <<= 1
	add	a,acc
	mov	b0,a
	mov	a,b1
	rlc	a
	mov	b1,a
	mov	a,b2
	rlc	a
	mov	b2,a
	mov	a,b3
	rlc	a
	mov	b3,a

	mov	a,a0		; a - b
	subb	a,b0		; here carry is always clear
	mov	a,a1
	subb	a,b1
	mov	a,a2
	subb	a,b2
	mov	a,a3
	subb	a,b3

	jnc	loop1


	clr	c
	mov	a,b3		; b >>= 1;
	rrc	a
	mov	b3,a
	mov	a,b2
	rrc	a
	mov	b2,a
	mov	a,b1
	rrc	a
	mov	b1,a
	mov	a,b0
	rrc	a
	mov	b0,a

loop2:	; clr	c		  never set
	mov	a,a0		; a - b
	subb	a,b0
	mov	r4,a
	mov	a,a1
	subb	a,b1
	mov	r5,a
	mov	a,a2
	subb	a,b2
	mov	r6,a
	mov	a,a3
	subb	a,b3

	jc	smaller		; a >= b?

	mov	a3,a		; -> yes;  a = a - b;
	mov	a2,r6
	mov	a1,r5
	mov	a0,r4
smaller:			; -> no
	clr	c
	mov	a,b3		; b >>= 1;
	rrc	a
	mov	b3,a
	mov	a,b2
	rrc	a
	mov	b2,a
	mov	a,b1
	rrc	a
	mov	b1,a
	mov	a,b0
	rrc	a
	mov	b0,a

	djnz	count,loop2

	mov	a,a3		; prepare the return value
div_by_0:
	ret

	__endasm;
}

#elif defined _MODULONG_ASM_SMALL_AUTO

static void
_modlong_dummy (void) __naked
{
	__asm

	.globl __modulong

__modulong:

	#define count   r0

	#define a0	dpl
	#define a1	dph
	#define a2	b
	#define a3	r1

	#define b0	r2
	#define b1	r3
	#define b2	r4
	#define b3	r5

	ar2 = 2			; BUG register set is not considered
	ar3 = 3
	ar4 = 4
	ar5 = 5

	.globl __modlong	; entry point for __modslong

				; parameter a comes in a, b, dph, dpl
	mov	a3,a		; save parameter a3

	mov	a,sp
	add	a,#-2-3		; 2 bytes return address, 3 bytes param b
	mov	r0,a		; r1 points to b0

	mov	ar2,@r0		; load b0
	inc	r0		; r0 points to b1
	mov	ar3,@r0		; b1
	inc	r0
	mov	ar4,@r0		; b2
	inc	r0
	mov	ar5,@r0		; b3

__modlong:			; entry point for __modslong
				; a in r1, b, dph, dpl
				; b in r5, r4, r3, r2

	mov	count,#0

	mov	a,b0		; b == 0? avoid endless loop
	orl	a,b1
	orl	a,b2
	orl	a,b3
	jz	div_by_0

	mov	count,#0
	clr	c		; when loop1 jumps immediately to loop2

loop1:	inc	count

	mov	a,b3		; if (!MSB_SET(b))
	jb	acc.7,loop2

	mov	a,b0		; b <<= 1
	add	a,acc
	mov	b0,a
	mov	a,b1
	rlc	a
	mov	b1,a
	mov	a,b2
	rlc	a
	mov	b2,a
	mov	a,b3
	rlc	a
	mov	b3,a

	mov	a,a0		; a - b
	subb	a,b0		; here carry is always clear
	mov	a,a1
	subb	a,b1
	mov	a,a2
	subb	a,b2
	mov	a,a3
	subb	a,b3

	jnc	loop1

	clr	c
	mov	a,b3		; b >>= 1;
	rrc	a
	mov	b3,a
	mov	a,b2
	rrc	a
	mov	b2,a
	mov	a,b1
	rrc	a
	mov	b1,a
	mov	a,b0
	rrc	a
	mov	b0,a

loop2:	; clr	c		  never set
	mov	a,a0		; a - b
	subb	a,b0
	mov	a,a1
	subb	a,b1
	mov	r6,a		; d1
	mov	a,a2
	subb	a,b2
	mov	r7,a		; d2
	mov	a,a3
	subb	a,b3

	jc	smaller		; a >= b?

	mov	a3,a		; -> yes;  a = a - b;
	mov	a2,r7
	mov	a1,r6
	mov	a,a0
	subb	a,b0
	mov	a0,a
smaller:			; -> no
	clr	c
	mov	a,b3		; b >>= 1;
	rrc	a
	mov	b3,a
	mov	a,b2
	rrc	a
	mov	b2,a
	mov	a,b1
	rrc	a
	mov	b1,a
	mov	a,b0
	rrc	a
	mov	b0,a

	djnz	count,loop2

	mov	a,a3		; prepare the return value
div_by_0:
	ret

	__endasm;
}

#elif defined(__SDCC_mcs251) && !defined(__SDCC_USE_XSTACK) && !defined(__SDCC_PARMS_IN_BANK1) && !defined(_SDCC_NO_ASM_LIB_FUNCS) && (defined(__SDCC_MODEL_SMALL) || defined(__SDCC_MODEL_LARGE))

/* 251 原生双字加法、比较和减法保持完整 32 位语义。
   DR0 保存被除数/商，DR4 保存除数，DR12 保存余数；A/B 分别别名
   R11/R10，不能用 DR8 保存跨 A/B 写入的状态。按除数最高非零字节
   预装被除数前缀，只循环 8/16/24/32 次；移入前已消费不足 32 位，故不溢出。
   参数与返回值沿用现有 ABI；只占用调用者保存的寄存器，不用静态暂存。
   非零除数下，16 位输入可由 DIV WR2,WR6 得到 WR2 商和 WR0 余数。
   除零是 C 未定义行为：运行库直接调用约定商全 1、余数为被除数，
   用于有界退出，不将此约定扩展为 C 语言对 /0 或 %0 的保证。 */
unsigned long
_modulong (unsigned long x, unsigned long y) __SDCC_NONBANKED __naked
{
  __asm
    mov r0,a
    mov r1,b
    mov r2,dph
    mov r3,dpl
#if defined(__SDCC_STACK_AUTO)
    mov wr4,@spx-0x0006
    mov wr6,@spx-0x0004
#elif defined(__SDCC_MODEL_SMALL)
    mov wr4,__modulong_PARM_2
    mov wr6,(__modulong_PARM_2 + 2)
#else
    mov dptr,#__modulong_PARM_2
    mov dpxl,#(__modulong_PARM_2 >> 16)
    mov wr4,@dpx
    inc dpx,#2
    mov wr6,@dpx
#endif
    cmp dr4,#0
    jne 00020$
    ejmp 00004$
00020$:
    cmp dr0,dr4
    jnc 00021$
    ejmp 00005$
00021$:
    cmp wr0,#0
    jne 00022$
    ejmp 00006$
00022$:
    cmp dr4,#1
    je 00009$
    mov dr12,dr0
    sub dr12,dr4
    cmp dr12,dr4
    jc 00010$
    cmp r4,#0
    jne 00011$
    cmp r5,#0
    jne 00012$
    cmp r6,#0
    jne 00013$
    mov dr12,#0
    mov b,#32
    sjmp 00001$
00011$:
    mov r12,#0
    mov r13,r0
    mov r14,r1
    mov r15,r2
    mov r0,r3
    mov r1,#0
    mov wr2,#0
    mov b,#8
    sjmp 00001$
00012$:
    mov wr12,#0
    mov wr14,wr0
    mov wr0,wr2
    mov wr2,#0
    mov b,#16
    sjmp 00001$
00013$:
    mov dr12,#0
    mov r15,r0
    mov r0,r1
    mov r1,r2
    mov r2,r3
    mov r3,#0
    mov b,#24
00001$:
    add dr0,dr0
    jc 00002$
    add dr12,dr12
    sjmp 00003$
00002$:
    add dr12,dr12
    inc dr12
00003$:
    cmp dr12,dr4
    jc 00007$
    sub dr12,dr4
    inc dr0
00007$:
    djnz b,00001$
    mov dr0,dr12
    sjmp 00008$
00009$:
    mov dr0,#0
    sjmp 00008$
00010$:
    mov dr0,dr12
    sjmp 00008$
00004$:
    sjmp 00008$
00005$:
    sjmp 00008$
00006$:
    div wr2,wr6
    mov wr2,wr0
    mov wr0,#0
00008$:
    mov dpl,r3
    mov dph,r2
    mov b,r1
    mov a,r0
    eret
  __endasm;
}

#else // _MODULONG_ASM

#define MSB_SET(x) ((x >> (8*sizeof(x)-1)) & 1)

unsigned long
_modulong (unsigned long a, unsigned long b) __SDCC_NONBANKED
{
  unsigned char count = 0;

  while (!MSB_SET(b))
  {
     b <<= 1;
     if (b > a)
     {
        b >>=1;
        break;
     }
     count++;
  }
  do
  {
    if (a >= b)
      a -= b;
    b >>= 1;
  }
  while (count--);

  return a;
}

#endif // _MODULONG_ASM
