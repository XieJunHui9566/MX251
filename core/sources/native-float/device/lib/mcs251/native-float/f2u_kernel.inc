; GPL-2.0-or-later WITH SDCC library exception, like bits_f2u.c.
; ABI4：DR4 为 binary32 输入与 uint32 返回；零栈、无调用、无共享状态。
; 保留旧策略：所有负位型和小于1的正值返回0；正溢出/Inf/NaN返回UINT_MAX。
___m251r4_bits_f2u:
    cmp wr4,#0x3f80
    jc fi_zero
    cmp wr4,#0x4f80
    jc fi_finite
    cmp r4,#0x80
    jnc fi_zero
    mov wr4,#0xffff
    mov wr6,#0xffff
    eret
fi_zero:
    sub dr4,dr4
    eret
fi_finite:
    ; 1<=x<2^32，e属于127..158。尾数移至bit31，统一做无符号右移。
    mov wr8,wr4
    add wr8,wr8
    mov a,#158
    sub a,r8
    mov r4,r5
    orl r4,#0x80
    mov r5,r6
    mov r6,r7
    mov r7,#0
    cmp a,#16
    jc fi_byte
    mov wr6,wr4
    sub wr4,wr4
    sub a,#16
fi_byte:
    cmp a,#8
    jc fi_bits
    mov r7,r6
    mov r6,r5
    mov r5,r4
    mov r4,#0
    sub a,#8
fi_bits:
    jz fi_done
fi_shift:
    srl wr6
    srl wr4
    jnc fi_shifted
    orl wr6,#0x8000
fi_shifted:
    dec a
    jnz fi_shift
fi_done:
    eret
; mx251-effects schema=1 function=___m251r4_bits_f2u abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=0 unknown=1 calls=0
