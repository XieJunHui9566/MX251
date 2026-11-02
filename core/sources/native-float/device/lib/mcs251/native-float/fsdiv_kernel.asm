; /*-------------------------------------------------------------------------
;    _fsdiv.c - Floating point library in optimized assembly for 8051
; 
;    Copyright (c) 2004, Paul Stoffregen, paul@pjrc.com
; 
;    This library is free software; you can redistribute it and/or modify it
;    under the terms of the GNU General Public License as published by the
;    Free Software Foundation; either version 2, or (at your option) any
;    later version.
; 
;    This library is distributed in the hope that it will be useful,
;    but WITHOUT ANY WARRANTY; without even the implied warranty of
;    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;    GNU General Public License for more details.
; 
;    You should have received a copy of the GNU General Public License
;    along with this library; see the file COPYING. If not, write to the
;    Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
;    MA 02110-1301, USA.
; 
;    As a special exception, if you link this library with other files,
;    some of which are compiled with SDCC, to produce an executable,
;    this library does not by itself cause the resulting executable to
;    be covered by the GNU General Public License. This exception does
;    not however invalidate any other reasons why the executable file
;    might be covered by the GNU General Public License.
; -------------------------------------------------------------------------*/
; 
; 

.module _fsdiv
.area CSEG (CODE)
.globl ___m251r4___fsdiv
.globl ___m251r4_mcs251_math_div_bits
; ABI4: DR4=x, DR12=y, DR4=result. No calls, no stack, no peripheral access.
; Registers: DR0 quotient / initial abs(x); DR4 remainder; DR12 divisor;
; WR24 exponent; WR26 sign; WR8 count; DR16/20/28 scratch; A scratch.
; Preserve first NaN payload/sign, quiet it; invalid results are +canonical qNaN.
___m251r4_mcs251_math_div_bits:
___m251r4___fsdiv:
    mov wr26,wr4
    xrl wr26,wr12
    anl wr26,#0x8000
    mov dr0,dr4
    anl wr0,#0x7fff
    mov dr28,dr12
    anl wr28,#0x7fff
    mov wr20,#0x7f80
    sub wr22,wr22
    cmp dr0,dr20
    jle div_check_nan_y
    orl wr4,#0x0040
    eret
div_check_nan_y:
    cmp dr28,dr20
    jle div_check_invalid_zero
    mov dr4,dr12
    orl wr4,#0x0040
    eret
div_check_invalid_zero:
    cmp dr0,#0
    jne div_check_inf_x
    cmp dr28,#0
    je div_invalid
div_check_inf_x:
    cmp dr0,dr20
    jne div_check_zero_y
    cmp dr28,dr20
    je div_invalid
    sjmp div_infinity
div_check_zero_y:
    cmp dr28,#0
    je div_infinity
    cmp dr0,#0
    je div_zero
    cmp dr28,dr20
    jne div_finite
div_zero:
    mov wr4,wr26
    sub wr6,wr6
    eret
div_infinity:
    mov wr4,wr26
    orl wr4,#0x7f80
    sub wr6,wr6
    eret
div_invalid:
    mov wr4,#0x7fc0
    sub wr6,wr6
    eret
div_finite:
    ; Exact exponent extraction from the two high words; sign shifts out.
    mov wr24,wr4
    add wr24,wr24
    mov wr10,wr24
    movz wr24,r10
    mov wr28,wr12
    add wr28,wr28
    mov wr10,wr28
    movz wr28,r10
    anl wr4,#0x007f
    anl wr12,#0x007f
    cmp wr24,#0
    je div_subnormal_x
    orl wr4,#0x0080
    sjmp div_normalize_y
div_subnormal_x:
    mov wr24,#1
    mov wr20,#0x0080
div_normalize_x_loop:
    cmp wr4,wr20
    jnc div_normalize_y
    add dr4,dr4
    dec wr24,#1
    sjmp div_normalize_x_loop
div_normalize_y:
    cmp wr28,#0
    je div_subnormal_y
    orl wr12,#0x0080
    sjmp div_exponent
div_subnormal_y:
    mov wr28,#1
    mov wr20,#0x0080
div_normalize_y_loop:
    cmp wr12,wr20
    jnc div_exponent
    add dr12,dr12
    dec wr28,#1
    sjmp div_normalize_y_loop
div_exponent:
    sub wr24,wr28
    add wr24,#0x007f
    cmp dr4,dr12
    jnc div_check_overflow
    add dr4,dr4
    dec wr24,#1
div_check_overflow:
    mov wr20,#0x00ff
    cmp wr24,wr20
    jsl div_check_underflow
    ejmp div_infinity
div_check_underflow:
    mov wr20,#0xffe9
    cmp wr24,wr20
    jsge div_check_half_min
    ejmp div_zero
div_check_half_min:
    jne div_precision
    cmp dr4,dr12
    jg div_above_half_min
    ejmp div_zero
div_above_half_min:
    mov wr4,wr26
    mov wr6,#1
    eret
div_precision:
    sub wr20,wr20
    cmp wr24,wr20
    jsle div_subnormal_precision
    mov wr8,#24
    sjmp div_start_quotient
div_subnormal_precision:
    mov wr8,wr24
    add wr8,#23
div_start_quotient:
    ; Normalization proves the first quotient bit is one. Remaining precision
    ; is 0..23, with remainder strictly less than the normalized divisor.
    mov dr0,#1
    sub dr4,dr12
    dec wr8,#1

div_radix_loop:
    mov wr20,#8
    cmp wr8,wr20
    jc div_bit_loop
    ; Shift the 24-bit exact remainder by 8 without a software shift loop.
    mov r4,r5
    mov r5,r6
    mov r6,r7
    mov r7,#0
    mov dr28,dr4
    ; DIV WR6,WR12 uses two 16-bit operands: WR6 quotient, WR4 remainder.
    ; It is NOT a 32/16 division. U_high16 / divisor_high8 overestimates
    ; the next byte by at most two; clamp before the exact 24x8 product.
    mov wr6,wr4
    div wr6,wr12
    mov wr20,#255
    cmp wr6,wr20
    jle div_digit_capped
    mov wr6,wr20
div_digit_capped:
    ; MUL WR16,WR6 returns a full product in DR16; likewise DR20 below.
    mov wr16,wr14
    mul wr16,wr6
    mov wr20,wr12
    mul wr20,wr6
    add wr16,wr22
div_correct_digit:
    cmp dr16,dr28
    jle div_digit_exact
    sub dr16,dr12
    dec wr6,#1
    sjmp div_correct_digit
div_digit_exact:
    mov a,r7
    mov dr4,dr28
    sub dr4,dr16
    mov r0,r1
    mov r1,r2
    mov r2,r3
    mov r3,a
    mov wr20,#8
    sub wr8,wr20
    sjmp div_radix_loop

div_bit_loop:
    cmp wr8,#0
    je div_round
    add dr4,dr4
    add dr0,dr0
    cmp dr4,dr12
    jc div_next_bit
    sub dr4,dr12
    orl wr2,#1
div_next_bit:
    dec wr8,#1
    sjmp div_bit_loop
div_round:
    ; Single nearest-even rounding at FINAL precision, including subnormal.
    add dr4,dr4
    cmp dr4,dr12
    jg div_increment
    jne div_pack
    mov a,r3
    anl a,#1
    jz div_pack
div_increment:
    inc dr0,#1
div_pack:
    sub wr20,wr20
    cmp wr24,wr20
    jsle div_pack_sign
    ; For two normalized 24-bit integer mantissas, rounded quotient <2^24.
    ; (exp-1)*2^23+q is the same encoding as exponent | fractional bits.
    dec wr24,#1
    add wr24,wr24
    add wr24,wr24
    add wr24,wr24
    add wr24,wr24
    add wr24,wr24
    add wr24,wr24
    add wr24,wr24
    add wr0,wr24
div_pack_sign:
    orl wr0,wr26
    mov dr4,dr0
    eret
; Conservative caller-visible effects: no synthesized leaf-save exemptions.
; mx251-effects schema=1 function=___m251r4___fsdiv abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=0
