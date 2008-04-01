/*
 *  Copyright (C) 2006 Giridhar Pemmasani
 *  Copyright (C) 2007-2010 Intel Corp
 *  	Contributed by Chandramouli Narayanan<mouli@linux.intel.com>
 *	Adapted wrapper macros for Linux to windows calls from
 *	NDIS wrapper project (http:/ndiswrapper.sourceforge.net)
 *	
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#define alloc_win_stack_frame(argc)		\
	"subq $" #argc "*8, %%rsp\n\t"
#define free_win_stack_frame(argc)		\
	"addq $" #argc "*8, %%rsp\n\t"

/* m is index of Windows arg required, n is total number of args to
 * function Windows arg 1 should be at 0(%rsp), arg 2 at 8(%rsp) and
 * so on, after stack frame is allocated, which starts at -n*8(%rsp)
 * when stack frame is allocated. 4 > m >= n.
*/

#define lin2win_win_arg(m,n) "(" #m "-1-" #n ")*8(%%rsp)"

/* volatile args for Windows function must be in clobber / output list */
extern unsigned long long x64_call0(void *func);
extern unsigned long long x64_call1(void *func, unsigned long long arg1);
extern unsigned long long x64_call2(void *func, unsigned long long arg1, unsigned long long arg2);
extern unsigned long long x64_call3(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3);
extern unsigned long long x64_call4(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4);
extern unsigned long long x64_call5(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5);
extern unsigned long long x64_call6(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6);
extern unsigned long long x64_call7(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7);
extern unsigned long long x64_call8(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8);
extern unsigned long long x64_call9(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8,
	unsigned long long arg9);
extern unsigned long long x64_call10(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8,
	unsigned long long arg9,
	unsigned long long arg10);


unsigned long long x64_call0(void *func)
{									
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8");
	register unsigned long long r9 __asm__("r9");
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(					
		alloc_win_stack_frame(4)				
		"call *%[fptr]\n\t"					
		free_win_stack_frame(4)					
		: "=a" (ret), "=c" (dummy), "=d" (dummy),		
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)		
		: [fptr] "r" (func));					
	return ret;								
}

unsigned long long x64_call1(void *func, unsigned long long arg1)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8");
	register unsigned long long r9 __asm__("r9");
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		alloc_win_stack_frame(4)
		"call *%[fptr]\n\t"
		free_win_stack_frame(4)	
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call2(void *func, unsigned long long arg1, unsigned long long arg2)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8");
	register unsigned long long r9 __asm__("r9");
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		alloc_win_stack_frame(4)
		"call *%[fptr]\n\t"
		free_win_stack_frame(4)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call3(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9");
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		alloc_win_stack_frame(4)
		"call *%[fptr]\n\t"
		free_win_stack_frame(4)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call4(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		alloc_win_stack_frame(4)
		"call *%[fptr]\n\t"
		free_win_stack_frame(4)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call5(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"mov %[rarg5], " lin2win_win_arg(5,6) "\n\t"
		alloc_win_stack_frame(6)
		"call *%[fptr]\n\t"
		free_win_stack_frame(6)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call6(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"movq %[rarg5], " lin2win_win_arg(5,6) "\n\t"
		"movq %[rarg6], " lin2win_win_arg(6,6) "\n\t"
		alloc_win_stack_frame(6)
		"call *%[fptr]\n\t"
		free_win_stack_frame(6)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5), [rarg6] "r" ((unsigned long long)arg6),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call7(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"movq %[rarg5], " lin2win_win_arg(5,7) "\n\t"
		"movq %[rarg6], " lin2win_win_arg(6,7) "\n\t"
		"movq %[rarg7], " lin2win_win_arg(7,7) "\n\t"
		alloc_win_stack_frame(7)
		"call *%[fptr]\n\t"
		free_win_stack_frame(7)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5), [rarg6] "r" ((unsigned long long)arg6),
		  [rarg7] "r" ((unsigned long long)arg7), [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call8(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"movq %[rarg5], " lin2win_win_arg(5,8) "\n\t"
		"movq %[rarg6], " lin2win_win_arg(6,8) "\n\t"
		"movq %[rarg7], " lin2win_win_arg(7,8) "\n\t"
		"movq %[rarg8], " lin2win_win_arg(8,8) "\n\t"
		alloc_win_stack_frame(8)
		"call *%[fptr]\n\t"
		free_win_stack_frame(8)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5), [rarg6] "r" ((unsigned long long)arg6),
		  [rarg7] "r" ((unsigned long long)arg7), [rarg8] "r" ((unsigned long long)arg8),
		  [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call9(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8,
	unsigned long long arg9)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"movq %[rarg5], " lin2win_win_arg(5,9) "\n\t"
		"movq %[rarg6], " lin2win_win_arg(6,9) "\n\t"
		"movq %[rarg7], " lin2win_win_arg(7,9) "\n\t"
		"movq %[rarg8], " lin2win_win_arg(8,9) "\n\t"
		"movq %[rarg9], " lin2win_win_arg(9,9) "\n\t"
		alloc_win_stack_frame(9)
		"call *%[fptr]\n\t"
		free_win_stack_frame(9)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5), [rarg6] "r" ((unsigned long long)arg6),
		  [rarg7] "r" ((unsigned long long)arg7), [rarg8] "r" ((unsigned long long)arg8),
		  [rarg9] "r" ((unsigned long long)arg9), [fptr] "r" (func));
	return ret;
}

unsigned long long x64_call10(
	void *func,
	unsigned long long arg1,
	unsigned long long arg2,
	unsigned long long arg3,
	unsigned long long arg4,
	unsigned long long arg5,
	unsigned long long arg6,
	unsigned long long arg7,
	unsigned long long arg8,
	unsigned long long arg9,
	unsigned long long arg10)
{
	unsigned long long ret, dummy;
	register unsigned long long r8 __asm__("r8") = (unsigned long long)arg3;
	register unsigned long long r9 __asm__("r9") = (unsigned long long)arg4;
	register unsigned long long r10 __asm__("r10");
	register unsigned long long r11 __asm__("r11");
	__asm__ __volatile__(
		"movq %[rarg5], " lin2win_win_arg(5,10) "\n\t"
		"movq %[rarg6], " lin2win_win_arg(6,10) "\n\t"
		"movq %[rarg7], " lin2win_win_arg(7,10) "\n\t"
		"movq %[rarg8], " lin2win_win_arg(8,10) "\n\t"
		"movq %[rarg9], " lin2win_win_arg(9,10) "\n\t"
		"movq %[rarg10], " lin2win_win_arg(10,10) "\n\t"
		alloc_win_stack_frame(10)
		"call *%[fptr]\n\t"
		free_win_stack_frame(10)
		: "=a" (ret), "=c" (dummy), "=d" (dummy),
		  "=r" (r8), "=r" (r9), "=r" (r10), "=r" (r11)
		: "c" (arg1), "d" (arg2), "r" (r8), "r" (r9),
		  [rarg5] "r" ((unsigned long long)arg5), [rarg6] "r" ((unsigned long long)arg6),
		  [rarg7] "r" ((unsigned long long)arg7), [rarg8] "r" ((unsigned long long)arg8),
		  [rarg9] "r" ((unsigned long long)arg9), [rarg10] "r" ((unsigned long long)arg10),
		  [fptr] "r" (func));
	return ret;
}

