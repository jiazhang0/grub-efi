
#define ENTRY(name) \
  ".globl " #name ";" \
  ".align 16;" \
  #name ":"

__asm__ __volatile__(
ENTRY(i386_call0)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$8, %%esp
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call1)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$20, %%esp
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call2)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$16, %%esp
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call3)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$12, %%esp
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call4)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$8, %%esp
	pushl	24(%%ebp)
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call5)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$20, %%esp
	pushl	28(%%ebp)
	pushl	24(%%ebp)
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_64_call5)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$12, %%esp
	pushl	36(%%ebp)
	pushl	32(%%ebp)
	pushl	28(%%ebp)
	pushl	24(%%ebp)
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");


__asm__ __volatile__(
ENTRY(i386_call6)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$16, %%esp
	pushl	32(%%ebp)
	pushl	28(%%ebp)
	pushl	24(%%ebp)
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");

__asm__ __volatile__(
ENTRY(i386_call7)"
	pushl	%%ebp
	movl	%%esp,%%ebp
	subl	$12, %%esp
	pushl	36(%%ebp)
	pushl	32(%%ebp)
	pushl	28(%%ebp)
	pushl	24(%%ebp)
	pushl	20(%%ebp)
	pushl	16(%%ebp)
	pushl	12(%%ebp)
	call	*8(%%ebp)
	leave
	ret
        ");
