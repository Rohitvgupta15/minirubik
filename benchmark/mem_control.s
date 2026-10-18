.global _start
.text
_start:
    li s0, 1000000       # Loop counter: 1,000,000 iterations
loop:
    addi s0, s0, -1      # Decrement
    bnez s0, loop        # Branch

    li a7, 93            # ecall exit
    li a0, 0
    ecall