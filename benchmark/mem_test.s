.globl _start
.text
_start:
    la   t0, buffer
    li   t1, 4194304
    li   t2, 0x55
loop:
    sb   t2, 0(t0)
    addi t0, t0, 1
    addi t1, t1, -1
    bnez t1, loop
    li   a7, 93
    li   a0, 0
    ecall
.bss
.align 4
buffer:
    .space 4194304