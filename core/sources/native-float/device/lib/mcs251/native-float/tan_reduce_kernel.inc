; GPL-2.0-or-later WITH SDCC library exception, like tancotf.c.
; ABI4: DR4=binary32, DR12=平坦24位输出指针，R7返回商奇偶性。
; 私有调用域保持原样：有限且 |x|>0.5；不改224位常数或舍入规则。
; 31字节局部积，保存原输入高字和输出指针共6字节。
; 依赖配方核对两个私有被调函数的寄存器保留集合；不匹配则保留原C。
; WR0=point, R2=符号/round_up/parity, WR26=top, DR28=product。
___m251r4_mcs251_reduce_fraction:
    push dr12
    push wr4
    add spx,#31
    mov dr12,spx
    sub dr12,#30
    anl wr12,#0x00ff
    mov dr28,dr12
    anl wr4,#0x007f
    orl wr4,#0x0080
    ecall ___m251r4_mcs251_multiply_two_over_pi
    mov wr0,@spx-32
    mov r2,r0
    anl r2,#0x80
    mov r8,r0
    add r8,r8
    cmp r1,#0x80
    jc tr_exponent
    inc r8
tr_exponent:
    mov wr0,#374
    movz wr20,r8
    sub wr0,wr20
    mov dr4,dr28
    mov wr14,wr0
    dec wr14
    ecall ___m251r4_mcs251_product_bit
    mov r3,r7
    mov dr4,dr28
    mov wr14,wr0
    ecall ___m251r4_mcs251_product_bit
    xrl r7,r3
    orl r2,r7
    mov r8,r3
    add r8,r8
    orl r2,r8
    cmp r3,#0
    je tr_top
    mov dr16,dr28
    mov r9,#31
    setb c
tr_complement:
    mov r11,@dr16
    xrl a,#0xff
    addc a,#0
    mov @dr16,r11
    inc dr16
    anl wr16,#0x00ff
    dec r9
    mov a,r9
    jnz tr_complement
tr_top:
    mov wr26,wr0
    dec wr26,#2
tr_search:
    mov dr4,dr28
    mov wr14,wr26
    ecall ___m251r4_mcs251_product_bit
    cmp r7,#0
    jne tr_found
    dec wr26
    sjmp tr_search
tr_found:
    mov wr14,wr26
    srl wr14
    srl wr14
    srl wr14
    mov r8,r15
    movz wr18,r15
    sub wr16,wr16
    add dr16,dr28
    anl wr16,#0x00ff
    sub dr4,dr4
    mov r7,@dr16
    mov r9,#3
tr_window:
    mov r4,r5
    mov r5,r6
    mov r6,r7
    mov r7,#0
    cmp r8,#0
    je tr_window_next
    dec r8
    dec dr16
    anl wr16,#0x00ff
    mov r7,@dr16
tr_window_next:
    dec r9
    cmp r9,#0
    jne tr_window
    mov wr10,wr26
    anl a,#7
    jz tr_aligned
tr_align:
    srl wr6
    srl wr4
    jnc tr_shifted
    orl wr6,#0x8000
tr_shifted:
    dec a
    jnz tr_align
tr_aligned:
    mov r8,r7
    anl r8,#1
    srl wr6
    srl wr4
    jnc tr_mantissa
    orl wr6,#0x8000
tr_mantissa:
    mov r4,#0
    cmp r8,#0
    je tr_round_done
    inc dr4
    cmp r4,#0
    je tr_round_done
    mov wr4,#0x0080
    sub wr6,wr6
    inc wr26
tr_round_done:
    add wr26,#127
    sub wr26,wr0
    mov wr10,wr26
    srl a
    mov r4,a
    anl r5,#0x7f
    jnc tr_pack_sign
    orl r5,#0x80
tr_pack_sign:
    mov r8,r2
    srl r8
    anl r8,#1
    cmp r8,#0
    je tr_sign
    xrl r2,#0x80
tr_sign:
    mov r8,r2
    anl r8,#0x80
    orl r4,r8
    sub spx,#31
    pop wr0
    pop dr12
    anl wr12,#0x00ff
    mov @dr12,wr4
    mov @dr12+2,wr6
    mov r7,r2
    anl r7,#1
    eret
; mx251-effects schema=1 function=___m251r4_mcs251_reduce_fraction abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=4
