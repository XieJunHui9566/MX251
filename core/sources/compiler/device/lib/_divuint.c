/*-------------------------------------------------------------------------
   _divuint.c - routine for unsigned int (16 bit) division

   Copyright (C) 1999, Jean-Louis Vern <jlvern AT gmail.com>

   This library is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
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

#include <stdbool.h>

#include <sdcc-lib.h>

#if !defined(__SDCC_USE_XSTACK) && !defined(_SDCC_NO_ASM_LIB_FUNCS)
#  if defined(__SDCC_mcs51)
#    if defined(__SDCC_MODEL_SMALL)
#      if defined(__SDCC_STACK_AUTO)
#        define _DIVUINT_ASM_SMALL_AUTO
#      else
#        define _DIVUINT_ASM_SMALL
#      endif
#    endif
#  endif
#endif

#if defined _DIVUINT_ASM_SMALL || defined _DIVUINT_ASM_SMALL_AUTO

static void
_divuint_dummy (void) __naked
{
	__asm

	.globl __divuint

__divuint:

	#define count   r2
	#define reste_l r3
	#define reste_h r4
	#define xl      dpl
	#define xh      dph

#if defined(__SDCC_PARMS_IN_BANK1)
	#define yl      (b1_0)
	#define yh      (b1_1)
#else // __SDCC_PARMS_IN_BANK1
  #if defined(__SDCC_STACK_AUTO)

	.globl __divint

	mov	a,sp
	add	a,#-2		; 2 bytes return address
	mov	r0,a		; r0 points to yh
	mov	a,@r0		; load yh
	mov	r1,a
	dec	r0
	mov	a,@r0		; load yl
	mov	r0,a

	#define yl      r0
	#define yh      r1

__divint:			; entry point for __divsint


  #else // __SDCC_STACK_AUTO

    #if defined(__SDCC_NOOVERLAY)
	.area DSEG    (DATA)
    #else
	.area OSEG    (OVR,DATA)
    #endif

	.globl __divuint_PARM_2
	.globl __divsint_PARM_2

__divuint_PARM_2:
__divsint_PARM_2:
	.ds	2

	.area CSEG    (CODE)

	#define yl      (__divuint_PARM_2)
	#define yh      (__divuint_PARM_2 + 1)

  #endif // __SDCC_STACK_AUTO
#endif // __SDCC_PARMS_IN_BANK1

	mov	count,#16
	clr	a
	mov	reste_l,a
	mov	reste_h,a

loop:
	mov	a,xl		; x <<= 1
	add	a,acc
	mov	xl,a
	mov	a,xh
	rlc	a
	mov	xh,a

	mov	a,reste_l	; reste <<= 1
	rlc	a		;   feed in carry
	mov	reste_l,a
	mov	a,reste_h
	rlc	a
	mov	reste_h,a

	mov	a,reste_l	; reste - y
	subb	a,yl		; here carry is always clear, because
					; reste <<= 1 never overflows
	mov	b,a
	mov	a,reste_h
	subb	a,yh

	jc	smaller		; reste >= y?

	mov	reste_h,a	; -> yes;  reste = reste - y;
	mov	reste_l,b
	orl	xl,#1
smaller:			; -> no
	djnz	count,loop
	ret

	__endasm;
}

#elif defined(__SDCC_mcs251) && !defined(__SDCC_USE_XSTACK) && !defined(__SDCC_PARMS_IN_BANK1) && !defined(_SDCC_NO_ASM_LIB_FUNCS) && (defined(__SDCC_MODEL_SMALL) || defined(__SDCC_MODEL_LARGE))

/* 251 的 DIV WR2,WR4 以 WR2 为被除数，返回 WR2 商、WR0 余数；
   除数 WR4 避开包含商和余数的 DR0。保留既有参数/返回 ABI，
   栈参数位于三字节 ECALL 返回地址之前，small/large 参数区不变。
   C 除零仍是未定义行为；仅为直接调用运行库定义有界退出：
   无符号商为全 1，余数为被除数，避免原 C 求余分支的死循环。 */
unsigned int
_divuint (unsigned int x, unsigned int y) __SDCC_NONBANKED __naked
{
  __asm
    mov r2,dph
    mov r3,dpl
#if defined(__SDCC_STACK_AUTO)
    mov wr4,@spx-0x0004
#elif defined(__SDCC_MODEL_SMALL)
    mov wr4,__divuint_PARM_2
#else
    mov dptr,#__divuint_PARM_2
    mov dpxl,#(__divuint_PARM_2 >> 16)
    mov wr4,@dpx
#endif
    cmp wr4,#0
    jne 00001$
    mov wr2,#0xffff
    sjmp 00002$
00001$:
    div wr2,wr4
00002$:
    mov dph,r2
    mov dpl,r3
    eret
  __endasm;
}

#else  // defined _DIVUINT_ASM_SMALL || defined _DIVUINT_ASM_SMALL_AUTO

#define MSB_SET(x) ((x >> (8*sizeof(x)-1)) & 1)

unsigned int
_divuint (unsigned int x, unsigned int y) __SDCC_NONBANKED
{
  unsigned int reste = 0;
  unsigned char count = 16;
  bool c;

  do
  {
    // reste: x <- 0;
    c = MSB_SET(x);
    x <<= 1;
    reste <<= 1;
    if (c)
      reste |= 1;

    if (reste >= y)
    {
      reste -= y;
      // x <- (result = 1)
      x |= 1;
    }
  }
  while (--count);
  return x;
}

#endif  // defined _DIVUINT_ASM_SMALL || defined _DIVUINT_ASM_SMALL_AUTO
