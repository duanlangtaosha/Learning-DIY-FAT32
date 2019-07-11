/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#ifndef XTYPES_H
#define XTYPES_H

#include <stdint.h>

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;
typedef uint32_t xfile_size_t;

typedef enum _xfat_err_t {
    FS_ERR_EOF = 1,
    FS_ERR_OK = 0,
    FS_ERR_IO = -1,
    FS_ERR_PARAM = -2,
    FS_ERR_NONE = -3,
    FS_ERR_FSTYPE = -4,
}xfat_err_t;

#endif

