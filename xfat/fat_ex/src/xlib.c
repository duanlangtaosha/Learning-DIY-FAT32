/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include "xlib.h"

void * xlib_memset(void* dest, u8_t v, u32_t size) {
	u8_t* d = (u8_t*)dest;

	while (size--) {
		*d++ = v;
	}

	return dest;
}

int xlib_memcmp(const void* ptr1, const void* ptr2, u32_t n) {
	u8_t * d1 = (u8_t*)ptr1;
	u8_t* d2 = (u8_t*)ptr2;

	while (n && (*d1 == *d2)) { 
		n--; 
		d1++;
		d2++;
	}

	return n ? (*d1 - *d2) : 0;
}

void* xlib_memcpy(void* ptr1, const void* ptr2, u32_t n) {
	u8_t* d1 = (u8_t*)ptr1;
	u8_t* d2 = (u8_t*)ptr2;

	while (n--) {
		*d1++ = *d2++;
	}

	return ptr1;
}

char* xlib_strncpy(char* dest, const char* src, u32_t n) {
	char* r = dest;

	// 超过长度的字符将填空
	while (n--) {
		*dest++ = *src ? *src++ : '\0';
	}

	return r;
}

int xlib_strncmp(const char* str1, const char* str2, u32_t n) {
	// 最多比较前n个字符
	while (n-- && *str1 && *str2) {}
	return 0;
}

int xlib_islower(int c) {
	return (c >= 'a') && (c <= 'z');
}

int xlib_toupper(int c) {
	return ((c >= 'a') && (c <= 'z')) ? (c - 'a' + 'A') : c;
}

int xlib_tolower(int c) {
	return ((c >= 'A') && (c <= 'Z')) ? (c - 'A' + 'a') : c;
}
