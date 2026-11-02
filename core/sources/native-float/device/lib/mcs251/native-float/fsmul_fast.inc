; ABI4 正规乘法入口；完整 48 位乘积及 RNE，异常值仍尾跳原有回退。
	.globl ___m251r4___fsmul
	.globl ___m251r4___fsmul_fallback
___m251r4___fsmul:
	mov dr0,dr4
	mov dr4,dr12
	mov a,r1
	mov c,acc.7
	mov a,r0
	rlc a
	jz 00191$
	cjne a,#255,00181$
	sjmp 00191$
00181$:
	movz wr8,a
	mov a,r5
	mov c,acc.7
	mov a,r4
	rlc a
	jz 00191$
	cjne a,#255,00182$
	sjmp 00191$
00182$:
    ; 正规指数和在[2,508]；减128后无符号小于253，等价于结果指数在[1,253]。
    ; 先保留为16位，负值模65536后必须拒绝，不能用8位回绕比较。
    movz wr12,a
    add wr8,wr12
    sub wr8,#128
    cmp wr8,#253
    jnc 00191$
    add wr8,#1
    sjmp 00192$
00191$:
	mov dr12,dr4
	mov dr4,dr0
	ejmp ___m251r4___fsmul_fallback
00192$:
	mov a,r0
	xrl a,r4
	anl a,#0x80
	push acc
	mov a,r1
	orl a,#0x80
	mov r0,a
	mov r1,r2
	mov r2,r3
	mov a,r5
	orl a,#0x80
	mov r4,a
	mov r5,r6
	mov r6,r7

    ; ma=ah*256+al, mb=bh*256+bl。交叉和最多 25 位，不会溢出 DR。
    mov wr12,wr0
    mul wr12,wr4
    push dr12
    mov a,r2
    mov b,r6
    mul ab
    mov r3,a
    mov r8,b
    movz wr12,r2
    mul wr12,wr4
    mov r2,r6
    mov dr4,dr12
    movz wr12,r2
    mul wr12,wr0
    add dr12,dr4
    mov wr4,#0
    movz wr6,r8
    add dr12,dr4
    ; DR12=ah*bl+al*bh+(al*bl>>8)，两低字节合并为 sticky。
    mov a,r15
    orl a,r3
    mov r7,a
    mov r15,r14
    mov r14,r13
    mov r13,r12
    mov r12,#0
    pop dr0
    add dr0,dr12
    mov a,r0
	jnb acc.7,00193$
	mov a,r3
	anl a,#1
	orl a,r7
	mov r7,a
    ; 先右移低字，再从高字移出的位补回低字最高位；此前已累积最低位sticky。
    srl wr2
    srl wr0
    jnc 00197$
    orl wr2,#0x8000
00197$:
	inc r9
00193$:
	mov a,r3
	anl a,#0x7f
	cjne a,#0x40,00194$
	mov a,r3
	anl a,#0x80
	orl a,r7
	jz 00196$
	sjmp 00195$
00194$:
	jc 00196$
00195$:
    add dr0,#0x80
00196$:
	mov a,r3
	mov c,acc.7
	mov a,r2
	rlc a
	mov r3,a
	mov a,r1
	rlc a
	mov r2,a
	mov a,r0
	rlc a
	mov r1,a
	clr a
	rlc a
	mov r0,a
	mov a,r9
	dec a
	clr c
	rrc a
	mov r12,a
	clr a
	rrc a
	add a,r1
	mov r1,a
	mov a,r12
	addc a,r0
	mov r0,a
	pop acc
	orl a,r0
	mov r0,a
	mov dr4,dr0
	eret
