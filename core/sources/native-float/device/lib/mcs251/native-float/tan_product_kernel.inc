; GPL-2.0-or-later WITH SDCC library exception, like tancotf.c.
; 精确计算 24 位有效数乘 224 位常量；两个 16 位遍历与 C 参考逐步等价。
; ABI4: DR4 significand, DR12 平坦 24 位输出指针。写且仅写 31 字节。
; 无栈、无调用、无静态可写状态。保持完整常量及舍入输入，不选芯片型号。
; WR24/WR26 保存有效数两半；DR16 是零扩展进位，DR0 是常量指针。
; DPX、A/B 和标志为易失资源；中断须按 ABI 保存自身修改的寄存器。
___m251r4_mcs251_multiply_two_over_pi:
    mov wr24,wr6
    mov wr26,wr4
    anl wr12,#0x00ff
    mov dr20,dr12
    mov a,#((_mcs251_two_over_pi+0)>>16)
    movz wr0,a
    mov wr2,#(_mcs251_two_over_pi+0)
    sub dr16,dr16
    mov r8,#14
__tan_product_low:
    mov dpx,dr0
    clr a
    movc a,@a+dptr
    mov b,a
    inc dpx
    clr a
    movc a,@a+dptr
    inc dr0,#2
    anl wr0,#0x00ff
    mov wr6,wr24
    mul wr6,wr10
    add dr4,dr16
    mov @dr12,r7
    inc dr12,#1
    anl wr12,#0x00ff
    mov @dr12,r6
    inc dr12,#1
    anl wr12,#0x00ff
    mov wr18,wr4
    dec r8
    cmp r8,#0
    jne __tan_product_low
    mov wr10,wr18
    mov @dr12,r11
    inc dr12,#1
    anl wr12,#0x00ff
    mov @dr12,r10
    mov dr12,dr20
    inc dr12,#2
    anl wr12,#0x00ff
    mov a,#((_mcs251_two_over_pi+0)>>16)
    movz wr0,a
    mov wr2,#(_mcs251_two_over_pi+0)
    sub dr16,dr16
    mov r8,#14
__tan_product_high:
    mov dpx,dr0
    clr a
    movc a,@a+dptr
    mov b,a
    inc dpx
    clr a
    movc a,@a+dptr
    inc dr0,#2
    anl wr0,#0x00ff
    mov wr6,wr26
    mul wr6,wr10
    add dr4,dr16
    mov r9,@dr12
    inc dr12,#1
    anl wr12,#0x00ff
    mov r10,@dr12
    mov a,r9
    mov wr22,wr10
    sub wr20,wr20
    add dr4,dr20
    mov @dr12,r6
    dec dr12,#1
    anl wr12,#0x00ff
    mov @dr12,r7
    inc dr12,#2
    anl wr12,#0x00ff
    mov wr18,wr4
    dec r8
    cmp r8,#0
    jne __tan_product_high
    mov wr10,wr18
    mov @dr12,r11
    eret
; mx251-effects schema=1 function=___m251r4_mcs251_multiply_two_over_pi abi=4 complete=0 reads=ffffffff writes=ffffffff specialReads=ffffffff specialWrites=ffffffff flagsReads=ffffffff flagsWrites=ffffffff memory=3 instructions=79 unknown=1 calls=0
