/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#ifndef Xbuf_H
#define Xbuf_H

#include "xtypes.h"

#define XFAT_BUF_STATE_FREE        (0 << 0)			// 扇区空闲，未被写入任何数据
#define XFAT_BUF_STATE_CLEAN       (1 << 0)			// 扇区干净，未被写数据
#define XFAT_BUF_STATE_DIRTY       (2 << 0)			// 扇区脏，已经被写入数据，未回写到磁盘
#define XFAT_BUF_STATE_MSK         (3 << 0)         // 写状态掩码

/**
 * 磁盘缓存区
 */
typedef struct _xfat_buf_t {
    u8_t * buf;                         // 数据缓冲区
    u32_t sector_no;                    // 扇区号
    u32_t flags;                        // 相关标记

    struct _xfat_buf_t * next;
    struct _xfat_buf_t * pre;
}xfat_buf_t;

#define xfat_buf_state(buf)       (buf->flags & XFAT_BUF_STATE_MSK)

void xfat_buf_set_state(xfat_buf_t * buf, u32_t state);

#endif //Xbuf_H
