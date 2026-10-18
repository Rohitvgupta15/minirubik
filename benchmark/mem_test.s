.global _start
.data
.align 2
buffer: .space 1048576   # 1 MiB guest memory allocation

.text
_start:
    la s0, buffer        # Load base address of buffer
    li s1, 1048576       # Buffer size in bytes
    li s2, 0x5A          # Pattern byte to store
write_loop:
    sb s2, 0(s0)         # Store byte to guest address
    addi s0, s0, 1       # Advance pointer by 1 byte
    addi s1, s1, -1      # Decrement byte counter
    bnez s1, write_loop  # Repeat until 1 MiB is populated

    li a7, 93            # ecall exit
    li a0, 0
    ecall