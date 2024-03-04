.global _start

_start:
        # sys_write call
        movq $1, %rax
        movq $1, %rdi
        leaq hello_world(%rip), %rsi
        movq $14, %rdx
        syscall

        # sys_exit call
        movq $60, %rax
        movq $69, %rdi
        syscall 

hello_world:
        .asciz "Hello, World!\n"
