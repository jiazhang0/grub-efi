#ifndef _GRUB_VA_LIST_H_
#define _GRUB_VA_LIST_H_

/*
 * Copyright (c) 1998  Intel Corporation
 */

#ifndef ASM_FILE
#ifdef __GLIBC__
#include <stdarg.h>
#else
#ifndef _STDARG_H
#define _INTSIZEOF(n)   ( (sizeof(n) + sizeof(unsigned int) - 1) & ~(sizeof(unsigned int) - 1) )

typedef unsigned char * va_list;

#define va_start(ap,v)  ( ap = (va_list)&v + _INTSIZEOF(v) )
#define va_arg(ap,t)    ( *(t *)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)) )
#define va_end(ap)  ( ap = (va_list)0 )
#endif /* _STDARG_H */
#endif /* __GLIBC__ */
#endif /* ASM_FILE */

#endif  /* _GRUB_VA_LIST_H_ */
