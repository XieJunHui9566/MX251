; GPL-2.0-or-later WITH SDCC library exception, like atanf.c.
; ABI4，参数/返回 DR4；所有算术仍调用原有 binary32 内核。
; 仅用私有栈保存跨调用值，不假设算术内核保留任何通用寄存器。
; 多项式各项和参数变换不合并舍入；原 C 与非常规 ABI 保留回退。
___m251r4_mcs251_atan_poly:
    push dr4
    mov dr12,dr4
    ecall ___m251r4_mcs251_math_mul_bits
    push dr4
    mov dr12,dr4
    mov wr4,#0xbd50
    mov wr6,#0x8691
    ecall ___m251r4_mcs251_math_mul_bits
    mov wr12,#0xbef1
    mov wr14,#0x10f6
    ecall ___m251r4_mcs251_math_add_bits
    pop dr12
    push dr12
    ecall ___m251r4_mcs251_math_mul_bits
    mov wr12,@spx-7
    mov wr14,@spx-5
    ecall ___m251r4_mcs251_math_mul_bits
    pop dr12
    push dr4
    mov dr4,dr12
    mov wr12,#0x3fb4
    mov wr14,#0xccd3
    ecall ___m251r4_mcs251_math_add_bits
    mov dr12,dr4
    pop dr4
    ecall ___m251r4_mcs251_math_div_bits
    mov dr12,dr4
    pop dr4
    ejmp ___m251r4_mcs251_math_add_bits
; mx251-effects schema=1 function=___m251r4_mcs251_atan_poly abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=7
.globl ___m251r4_atanf
___m251r4_atanf:
___m251r4_mcs251_atan_bits:
    mov r9,r4
    anl r9,#0x80
    anl r4,#0x7f
    cmp wr4,#0x7f80
    jc af_ordered
    jne af_nan
    cmp wr6,#0
    je af_ordered
af_nan:
    mov r9,#0
    ejmp af_reduced
af_ordered:
    cmp dr4,#0
    jne af_nonzero
    mov r9,#0
af_nonzero:
    cmp wr4,#0x3f80
    jc af_small
    jne af_reciprocal
    cmp wr6,#0
    je af_small
af_reciprocal:
    orl r9,#2
    push r9
    mov dr12,dr4
    mov wr4,#0x3f80
    mov wr6,#0
    ecall ___m251r4_mcs251_math_div_bits
    pop r9
af_small:
    cmp wr4,#0x3e89
    jc af_reduced
    jne af_transform
    cmp wr6,#0x30a4
    jc af_reduced
af_transform:
    inc r9
    push r9
    push dr4
    mov dr12,dr4
    mov wr4,#0x3fdd
    mov wr6,#0xb3d7
    ecall ___m251r4_mcs251_math_add_bits
    push dr4
    mov wr12,@spx-7
    mov wr14,@spx-5
    mov wr4,#0x3f3b
    mov wr6,#0x67af
    ecall ___m251r4_mcs251_math_mul_bits
    mov wr12,#0x3f80
    mov wr14,#0
    ecall ___m251r4_mcs251_math_sub_bits
    mov wr12,@spx-7
    mov wr14,@spx-5
    ecall ___m251r4_mcs251_math_add_bits
    pop dr12
    sub spx,#4
    ecall ___m251r4_mcs251_math_div_bits
    pop r9
af_reduced:
    mov wr12,wr4
    anl wr12,#0x7fff
    cmp wr12,#0x3980
    jc af_poly_done
    push r9
    ecall ___m251r4_mcs251_atan_poly
    pop r9
af_poly_done:
    mov a,r9
    anl a,#2
    jz af_quadrant
    xrl r4,#0x80
af_quadrant:
    mov a,r9
    anl a,#1
    jz af_even
    mov wr12,#0x3f06
    mov wr14,#0x0a92
    mov a,r9
    anl a,#2
    jz af_finish
    orl r13,#0x80
    sjmp af_finish
af_even:
    sub dr12,dr12
    mov a,r9
    anl a,#2
    jz af_finish
    mov wr12,#0x3fc9
    mov wr14,#0x0fdb
af_finish:
    push r9
    ecall ___m251r4_mcs251_math_add_bits
    pop r9
    anl r9,#0x80
    xrl r4,r9
    eret
; mx251-effects schema=1 function=___m251r4_mcs251_atan_bits abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=8
