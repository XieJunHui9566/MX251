; 无符号整数归一化到 bit31，按整字/字节跳过前导零。
; 旧库采用正整数向上取整；低字节只作为 sticky，不能改成 nearest-even。
; ABI4: DR4 输入/输出；仅写 R4..R7、R9、A，属于旧 clobber 的子集。
; 无栈、调用、SFR、共享数据；ABI2 桥由既有配方原样追加。
___m251r4_bits_u2f:
    cmp dr4,#0
    jne u2f_nonzero
    eret
u2f_nonzero:
    mov r9,#158
    cmp wr4,#0
    jne u2f_byte
    mov wr4,wr6
    sub wr6,wr6
    sub r9,#16
u2f_byte:
    cmp r4,#0
    jne u2f_bit
    mov r4,r5
    mov r5,r6
    mov r6,r7
    mov r7,#0
    sub r9,#8
u2f_bit:
    cmp r4,#0x80
    jnc u2f_normalized
    add dr4,dr4
    dec r9
    sjmp u2f_bit
u2f_normalized:
    mov a,r7
    mov r7,r6
    mov r6,r5
    mov r5,r4
    mov r4,#0
    cmp a,#0
    je u2f_pack
    inc dr4
    cmp r4,#0
    je u2f_pack
    mov r5,#0x80
    inc r9
u2f_pack:
    anl r5,#0x7f
    srl r9
    mov r4,r9
    jnc u2f_done
    orl r5,#0x80
u2f_done:
    eret
; 保留旧 effects 的保守超集，既有调用者无需重编译。
; mx251-effects schema=1 function=___m251r4_bits_u2f abi=4 complete=1 reads=00f0faf0 writes=00f0faf0 specialReads=00000004 specialWrites=00000004 flagsReads=ffffffff flagsWrites=ffffffff memory=1 instructions=39 unknown=0 calls=0
