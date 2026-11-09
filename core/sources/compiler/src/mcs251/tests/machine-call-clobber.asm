; ABI2 void provider: overwrite every GPR independently of C allocation.
    .module machine_call_clobber
    .globl _barrier
    .area CSEG (CODE)
_barrier:
    mov dr0,#0x1111
    mov dr4,#0x2222
    mov dr8,#0x3333
    mov dr12,#0x4444
    mov dr16,#0x5555
    mov dr20,#0x6666
    mov dr24,#0x7777
    mov dr28,#0x8888
    eret
