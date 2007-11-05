#define ENTRY(name) \
  .globl name; \
  .align 16; \
  name:

ENTRY(x64_call0)
        subq $40, %rsp
        call *%rdi
        addq $40, %rsp
        ret

ENTRY(x64_call1)
        subq $40, %rsp
        mov  %rsi, %rcx
        call *%rdi
        addq $40, %rsp
        ret

ENTRY(x64_call2)
        subq $40, %rsp
        mov  %rsi, %rcx
        call *%rdi
        addq $40, %rsp
        ret

ENTRY(x64_call3)
        subq $40, %rsp
        mov  %rcx, %r8
        mov  %rsi, %rcx
        call *%rdi
        addq $40, %rsp
        ret
ENTRY(x64_call4)
        subq $40, %rsp
        mov %r8, %r9
        mov %rcx, %r8
        mov %rsi, %rcx
        call *%rdi
        addq $40, %rsp
        ret

ENTRY(x64_call5)
        subq $40, %rsp
        mov %r9, 32(%rsp)
        mov %r8, %r9
        mov %rcx, %r8
        mov %rsi, %rcx
        call *%rdi
        addq $40, %rsp
        ret

ENTRY(x64_call6)
        subq $56, %rsp
        mov 56+8(%rsp), %rax
        mov %r9, 32(%rsp)
        mov %rax, 40(%rsp)
        mov %r8, %r9
        mov %rcx, %r8
        mov %rsi, %rcx
        call *%rdi
        addq $56, %rsp
        ret

ENTRY(x64_call7)
        subq $64, %rsp
        mov %r9, 32(%rsp)
        mov 64+8(%rsp), %rax
        mov %rax, 40(%rsp)
	mov 64+16(%rsp), %rax
	mov %rax, 48(%rsp)
        mov %r8, %r9
        mov %rcx, %r8
        mov %rsi, %rcx
        call *%rdi
        addq $64, %rsp
        ret

ENTRY(x64_call10)
        addq $88, %rsp
        mov  %r9, 32(%rsp)
        mov 88+8(%rsp), %rax
        mov %rax, 40(%rsp)
        mov 88+16(%rsp), %rax
        mov %rax, 48(%rsp)
        mov 88+24(%rsp), %rax
        mov %rax, 56(%rsp)
        mov 88+32(%rsp), %rax
        mov %rax, 64(%rsp)
        mov 88+40(%rsp), %rax
        mov %rax, 72(%rsp)
        call *%rdi
        addq $88, %rsp
        ret

