.globl _start
.text
_start:
    li   t0, 10000000
loop:
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 93
    li   a0, 0
    ecall