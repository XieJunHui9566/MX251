; /*-------------------------------------------------------------------------
;    _fsmul.c - Floating point library in optimized assembly for 8051
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

.module _fsadd
.area CSEG (CODE)
.globl ___m251r4___fsadd
.globl ___m251r4_mcs251_math_add_bits
; ABI4: DR4=x, DR12=y, DR4=result. All registers used are ABI4 volatile.
; No calls, stack frame, external memory or peripheral state. Keep conservative effects.
; WR24 sign, WR26 opposite; DR4 large magnitude, DR12 aligned small magnitude;
; WR0 effective exponent, WR8 distance; DR16/20 and A/R10 scratch.
___m251r4_mcs251_math_add_bits:
___m251r4___fsadd:
    mov dr0,dr4
    anl wr0,#0x7fff
    mov dr16,dr12
    anl wr16,#0x7fff
    mov wr20,#0x7f80
    sub wr22,wr22
    cmp dr0,dr20
    jle add_nan_y
    orl wr4,#0x0040
    eret
add_nan_y:
    cmp dr16,dr20
    jle add_inf_x
    mov dr4,dr12
    orl wr4,#0x0040
    eret
add_inf_x:
    cmp dr0,dr20
    jne add_inf_y
    cmp dr16,dr20
    jne add_return_x
    mov wr24,wr4
    xrl wr24,wr12
    anl wr24,#0x8000
    cmp wr24,#0
    je add_return_x
    mov wr4,#0x7fc0
    sub wr6,wr6
    eret
add_inf_y:
    cmp dr16,dr20
    je add_return_y
    cmp dr0,#0
    jne add_zero_y
    cmp dr16,#0
    jne add_return_y
    anl wr4,wr12
    anl wr4,#0x8000
    sub wr6,wr6
    eret
add_zero_y:
    cmp dr16,#0
    jne add_finite
add_return_x:
    eret
add_return_y:
    mov dr4,dr12
    eret
add_finite:
    mov wr24,wr4
    anl wr24,#0x8000
    mov wr26,wr4
    xrl wr26,wr12
    anl wr26,#0x8000
    mov dr4,dr0
    mov dr12,dr16
    cmp dr4,dr12
    jnc add_sorted
    mov dr4,dr16
    mov dr12,dr0
    xrl wr24,wr26
add_sorted:
    mov wr10,wr4
    add wr10,wr10
    movz wr0,r10
    mov wr10,wr12
    add wr10,wr10
    movz wr2,r10
    anl wr4,#0x007f
    anl wr12,#0x007f
    cmp wr0,#0
    je add_subnormal_a
    orl wr4,#0x0080
    sjmp add_prepare_b
add_subnormal_a:
    mov wr0,#1
add_prepare_b:
    cmp wr2,#0
    je add_subnormal_b
    orl wr12,#0x0080
    sjmp add_mantissas
add_subnormal_b:
    mov wr2,#1
add_mantissas:
    add dr4,dr4
    add dr4,dr4
    add dr4,dr4
    add dr12,dr12
    add dr12,dr12
    add dr12,dr12
    mov wr8,wr0
    sub wr8,wr2
    cmp wr8,#27
    jc add_alignment
    mov dr12,#1
    ejmp add_arithmetic
add_alignment:

    ; 8-bit jam preserves the OR of every discarded bit, including earlier sticky.
add_byte_loop:
    cmp wr8,#8
    jc add_bit_loop
    mov a,r15
    mov r15,r14
    mov r14,r13
    mov r13,r12
    mov r12,#0
    jz add_byte_no_sticky
    orl r15,#1
add_byte_no_sticky:
    dec wr8,#4
    dec wr8,#4
    sjmp add_byte_loop

add_bit_loop:
    cmp wr8,#0
    je add_arithmetic
    mov a,r15
    anl a,#1
    srl wr14
    srl wr12
    jnc add_bit_no_carry
    orl wr14,#0x8000
add_bit_no_carry:
    orl r15,a
    dec wr8,#1
    sjmp add_bit_loop
add_arithmetic:
    cmp wr26,#0
    jne add_subtract
    add dr4,dr12
    cmp wr4,#0x0800
    jc add_round
    mov a,r7
    anl a,#1
    srl wr6
    srl wr4
    jnc add_sum_no_carry
    orl wr6,#0x8000
add_sum_no_carry:
    orl r7,a
    inc wr0,#1
    sjmp add_round
add_subtract:
    sub dr4,dr12
    cmp dr4,#0
    jne add_normalize
    eret
add_normalize:

    ; Cancellation may normalize by eight only when it cannot overshoot bit26
    ; and the effective exponent remains at least one (subnormal boundary).
add_normalize_byte:
    cmp wr4,#4
    jnc add_normalize_bit
    cmp wr0,#8
    jle add_normalize_bit
    mov r4,r5
    mov r5,r6
    mov r6,r7
    mov r7,#0
    dec wr0,#4
    dec wr0,#4
    sjmp add_normalize_byte

add_normalize_bit:
    cmp wr4,#0x0400
    jnc add_round
    cmp wr0,#1
    jle add_round
    add dr4,dr4
    dec wr0,#1
    sjmp add_normalize_bit
add_round:
    cmp wr0,#255
    jc add_round_finite
    mov wr4,wr24
    orl wr4,#0x7f80
    sub wr6,wr6
    eret
add_round_finite:
    mov r9,r7
    anl r9,#7
    cmp r9,#4
    jg add_increment
    jne add_pack
    mov a,r7
    anl a,#8
    jz add_pack
add_increment:
    add dr4,#8
add_pack:
    srl wr6
    srl wr4
    jnc add_pack_nc_0
    orl wr6,#0x8000
add_pack_nc_0:
    srl wr6
    srl wr4
    jnc add_pack_nc_1
    orl wr6,#0x8000
add_pack_nc_1:
    srl wr6
    srl wr4
    jnc add_pack_nc_2
    orl wr6,#0x8000
add_pack_nc_2:
    dec wr0,#1
    add wr0,wr0
    add wr0,wr0
    add wr0,wr0
    add wr0,wr0
    add wr0,wr0
    add wr0,wr0
    add wr0,wr0
    add wr4,wr0
    orl wr4,wr24
    eret
; mx251-effects schema=1 function=___m251r4___fsadd abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=0
