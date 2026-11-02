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

.module _fsmul
.area CSEG (CODE)
.globl ___m251r4___fsmul
.globl ___m251r4_mcs251_math_mul_bits
; ABI4: DR4=x, DR12=y, DR4=result; only ABI4 volatile registers.
; 无栈、无调用、无外设状态。精确48位乘积在最终精度处只舍入一次。
; WR24保存符号，WR26保存带符号指数；R9合并已丢弃位的sticky。
___m251r4_mcs251_math_mul_bits:
___m251r4___fsmul:
    mov wr24,wr4
    xrl wr24,wr12
    anl wr24,#0x8000
    mov wr10,wr4
    add wr10,wr10
    movz wr0,r10
    mov wr10,wr12
    add wr10,wr10
    movz wr2,r10
    cmp wr0,#255
    jne mul_check_y
    ejmp mul_special
mul_check_y:
    cmp wr2,#255
    jne mul_finite
    ejmp mul_special
mul_finite:
    anl wr4,#0x007f
    anl wr12,#0x007f
    cmp wr0,#0
    je mul_subnormal_x
    orl wr4,#0x0080
    sjmp mul_prepare_y
mul_subnormal_x:
    cmp dr4,#0
    jne mul_normalize_x_start
    mov wr4,wr24
    sub wr6,wr6
    eret
mul_normalize_x_start:
    mov wr0,#1
mul_normalize_x:
    add dr4,dr4
    dec wr0,#1
    cmp wr4,#0x0080
    jc mul_normalize_x
mul_prepare_y:
    cmp wr2,#0
    je mul_subnormal_y
    orl wr12,#0x0080
    sjmp mul_product
mul_subnormal_y:
    cmp dr12,#0
    jne mul_normalize_y_start
    mov wr4,wr24
    sub wr6,wr6
    eret
mul_normalize_y_start:
    mov wr2,#1
mul_normalize_y:
    add dr12,dr12
    dec wr2,#1
    cmp wr12,#0x0080
    jc mul_normalize_y
mul_product:
    mov wr26,wr0
    add wr26,wr2
    sub wr26,#127
    ; a=256*ah+al，b=256*bh+bl。四次原生16位乘法结果均完整保留。
    mov r4,r5
    mov r5,r6
    mov r12,r13
    mov r13,r14
    mov wr16,wr4
    mul wr16,wr12
    movz wr20,r7
    movz wr22,r15
    mul wr20,wr22
    mov wr8,wr22
    movz wr20,r7
    mul wr20,wr12
    movz wr0,r15
    mul wr0,wr4
    add dr20,dr0
    movz wr2,r8
    sub wr0,wr0
    add dr20,dr0
    mov dr12,dr20
    ; 低乘积进位先加入交叉和；两个最低字节仅在此后合并为sticky。
    orl r9,r15
    mov r15,r14
    mov r14,r13
    mov r13,r12
    mov r12,#0
    add dr16,dr12
    mov dr4,dr16
    cmp wr4,#0x8000
    jc mul_exponent
    mov r10,r7
    anl r10,#1
    orl r9,r10
    srl wr6
    srl wr4
    jnc mul_product_no_carry
    orl wr6,#0x8000
mul_product_no_carry:
    inc wr26,#1
mul_exponent:
    cmp wr26,#255
    jsl mul_check_underflow
    mov wr4,wr24
    orl wr4,#0x7f80
    sub wr6,wr6
    eret
mul_check_underflow:
    cmp wr26,#0
    jsg mul_round
    cmp wr26,#-23
    jsge mul_underflow
    mov wr4,wr24
    sub wr6,wr6
    eret
mul_underflow:
    ; 最多24步；次正规输出只在最终精度舍入，不能先舍入正规尾数。
    mov r10,r7
    anl r10,#1
    orl r9,r10
    srl wr6
    srl wr4
    jnc mul_underflow_no_carry
    orl wr6,#0x8000
mul_underflow_no_carry:
    inc wr26,#1
    cmp wr26,#0
    jsle mul_underflow
    sub wr26,wr26
mul_round:
    mov r10,r7
    anl r10,#0x7f
    cmp r10,#0x40
    jg mul_increment
    jne mul_pack
    mov a,r7
    anl a,#0x80
    orl a,r9
    jz mul_pack
mul_increment:
    add dr4,#0x80
mul_pack:
    ; hi>>7 = ((hi<<1)>>8) | (carry<<24)。必须保留舍入产生的第25位。
    add dr4,dr4
    mov r7,r6
    mov r6,r5
    mov r5,r4
    mov r4,#0
    jnc mul_pack_exponent
    mov r4,#1
mul_pack_exponent:
    cmp wr26,#0
    je mul_pack_sign
    dec wr26,#1
    add wr26,wr26
    add wr26,wr26
    add wr26,wr26
    add wr26,wr26
    add wr26,wr26
    add wr26,wr26
    add wr26,wr26
    add wr4,wr26
mul_pack_sign:
    orl wr4,wr24
    eret
mul_special:
    ; 保留第一个NaN的符号/payload并quiet；0*Inf固定为正qNaN。
    mov dr0,dr4
    anl wr0,#0x7fff
    mov dr16,dr12
    anl wr16,#0x7fff
    mov wr20,#0x7f80
    sub wr22,wr22
    cmp dr0,dr20
    jle mul_special_y
    orl wr4,#0x0040
    eret
mul_special_y:
    cmp dr16,dr20
    jle mul_special_zero
    mov dr4,dr12
    orl wr4,#0x0040
    eret
mul_special_zero:
    cmp dr0,#0
    je mul_invalid
    cmp dr16,#0
    je mul_invalid
    mov wr4,wr24
    orl wr4,#0x7f80
    sub wr6,wr6
    eret
mul_invalid:
    mov wr4,#0x7fc0
    sub wr6,wr6
    eret
; mx251-effects schema=1 function=___m251r4___fsmul abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=0
