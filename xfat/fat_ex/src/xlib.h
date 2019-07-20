/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#ifndef XFAT_LIB_H
#define XFAT_LIB_H

#include "xtypes.h"

#define xlib_assert(expr, ...)	do {	\
	int c = (expr);	\
	if (!c) {printf("\n---- error ---\n%s %d %s failed:"#expr"\n"#__VA_ARGS__"\n",  \
			__FILE__, __LINE__, __func__); exit(c);}	\
} while (0)

#define xlib_abort(expr, ...) do {	\
	int c = (expr);	\
	if (!c) {printf("\n---- error ---\n%s %d %s failed:"#expr"\n"#__VA_ARGS__"\n",  \
			__FILE__, __LINE__, __func__); exit(c);}	\
} while (0)

void * xlib_memset(void * dest, u8_t v, u32_t size);
int xlib_memcmp(const void* ptr1, const void* ptr2, u32_t n);
void* xlib_memcpy(void* ptr1, const void* ptr2, u32_t n);
char* xlib_strncpy(char* dest, const char* src, u32_t n);
int xlib_strncmp(const char* str1, const char* str2, u32_t n);

int xlib_islower(int c);
int xlib_toupper(int c);
int xlib_tolower(int c);

#endif

