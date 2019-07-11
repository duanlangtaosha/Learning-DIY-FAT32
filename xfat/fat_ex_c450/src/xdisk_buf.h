//
// Created by Administrator on 2019/2/3.
//

#ifndef XDISK_BUF_H
#define XDISK_BUF_H

#include "xtypes.h"

#define BUFFER_FLAG_FREE        (0 << 0)			// 扇区空闲，未被写入任何数据
#define BUFFER_FLAG_CLEAN       (1 << 0)			// 扇区干净，未被写数据
#define BUFFER_FLAG_DIRTY       (2 << 0)			// 扇区脏，已经被写入数据，未回写到磁盘
#define BUFFER_FLAG_STATE_MSK   (3 << 0)            // 写状态掩码

#define BUFFER_FLAG_TYPE_SEC     (0 << 8)			// 用于扇区读写的缓存
#define BUFFER_FLAG_TYPE_WORK    (1 << 8)			// 用于内部工作的缓存
#define BUFFER_FLAG_TYPE_MSK     (1 << 8)

/**
 * 磁盘缓存区
 */
typedef struct _xdisk_buf_t {
    u8_t * buf;                         // 数据缓冲区
    u32_t sector_no;                    // 扇区号
    u32_t flags;                        // 相关标记

    struct _xdisk_buf_t * next;
    struct _xdisk_buf_t * pre;
}xdisk_buf_t;

#define xdisk_buf_state(disk_buf)       ((disk_buf->flags) & BUFFER_FLAG_STATE_MSK)
#define xdisk_buf_type(disk_buf)        (disk_buf->flags & BUFFER_FLAG_TYPE_MSK)

void xdisk_buf_set_type(xdisk_buf_t * disk_buf, u32_t type);
void xdisk_buf_set_state(xdisk_buf_t * disk_buf, u32_t state);

struct _xdisk_t;

/**
 * 磁盘缓存池
 */
typedef struct _xdisk_buf_list_t {
    xdisk_buf_t * first;
    xdisk_buf_t * last;
}xdisk_buf_pool_t;

// 磁盘缓存空间大小计算
#define DISK_BUF_SIZE(sector_size, sector_nr)    ((sizeof(xdisk_buf_t) + (sector_size)) * sector_nr)

xfat_err_t xdisk_buf_pool_init(xdisk_buf_pool_t * pool, u32_t sector_size, u8_t * buffer, u32_t buf_size);
xfat_err_t xdisk_buf_pool_alloc(xdisk_buf_pool_t * pool, u32_t type, u32_t sector_no, xdisk_buf_t ** disk_buf);
xfat_err_t xdisk_buf_pool_release(xdisk_buf_pool_t * pool, xdisk_buf_t * disk_buf);

typedef xfat_err_t(*xdisk_buf_acc_t)(xdisk_buf_t * disk_buf, void * arg);
xfat_err_t xdisk_buf_pool_trans_acc(xdisk_buf_pool_t * pool, xdisk_buf_acc_t acc, void * arg);

#endif //XDISK_BUF_H
