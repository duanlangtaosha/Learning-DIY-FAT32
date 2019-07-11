//
// Created by Administrator on 2019/2/3.
//

#include "xdisk_buf.h"
#include "xdisk.h"

/**
 * 获取buf的类型
 * @param disk_buf
 * @param type
 */
void xdisk_buf_set_type(xdisk_buf_t * disk_buf, u32_t type) {
    disk_buf->flags &= ~BUFFER_FLAG_TYPE_MSK;
    disk_buf->flags |= type;
}

/**
 * 获取buf的状态
 * @param disk_buf
 * @param state
 */
void xdisk_buf_set_state(xdisk_buf_t * disk_buf, u32_t state) {
    disk_buf->flags &= ~BUFFER_FLAG_STATE_MSK;
    disk_buf->flags |= state;
}

/**
 * 初始化磁盘缓存区
 * @param pool
 * @param disk
 * @param buffer
 * @param buf_size
 * @return
 */
xfat_err_t xdisk_buf_pool_init(xdisk_buf_pool_t * pool, u32_t sector_size, u8_t * buffer, u32_t buf_size) {
    u32_t buf_count = buf_size / (sizeof(xdisk_buf_t) + sector_size);
    u32_t i;
    xdisk_buf_t * disk_buf_start = (xdisk_buf_t *)buffer;
    u8_t * sector_buf_start = buffer + buf_count * sizeof(xdisk_buf_t);      // 这里最好做下对齐处理

    // 头插法建立链表
    pool->first = pool->last = (xdisk_buf_t *)0;
    for (i = 0; i < buf_count; i++) {
        xdisk_buf_t * buf = disk_buf_start++;
        buf->next = pool->first;
        buf->pre = (xdisk_buf_t *)0;

        if (buf->next) {
            buf->next->pre = buf;
        }

        pool->first = buf;
        if (pool->last == (xdisk_buf_t *)0) {
            pool->last = buf;
        }

        buf->sector_no = 0;
        buf->buf = sector_buf_start;
        buf->flags = BUFFER_FLAG_FREE;
        sector_buf_start += sector_size;
    }

    return FS_ERR_OK;
}

/**
 * 从缓冲列表中分配一个缓存块
 * @param pool
 * @param sector_no
 * @return
 */
xfat_err_t xdisk_buf_pool_alloc(xdisk_buf_pool_t * pool, u32_t type, u32_t sector_no, xdisk_buf_t ** disk_buf) {
    xfat_err_t err;
    xdisk_buf_t * r_disk_buf;

    if (pool->first == (xdisk_buf_t *)0) {
        return FS_ERR_NO_BUFFER;
    }

    if (type == BUFFER_FLAG_TYPE_WORK) {
        r_disk_buf = pool->last;
    } else {
        // 从表头开始查找，找到相同扇区的块，或者找到空闲块
        // 或者以上都没有，找到最久未被使用的块，此时肯定是队列最后一块
        r_disk_buf = pool->first;
        while (r_disk_buf) {
            if (xdisk_buf_state(r_disk_buf) == BUFFER_FLAG_FREE) {
                break;
            } else if (r_disk_buf->sector_no == sector_no) {
                break;
            }

            r_disk_buf = r_disk_buf->next;
        }

        // 遍历整个队列未找到，取队列尾部
        if (r_disk_buf == (xdisk_buf_t *)0) {
            r_disk_buf = pool->last;
        }
    }

    // 移除块
    if (r_disk_buf->pre) {
        r_disk_buf->pre->next = r_disk_buf->next;
    }

    if (r_disk_buf->next) {
        r_disk_buf->next->pre = r_disk_buf->pre;
    }

    if (r_disk_buf == pool->first) {
        pool->first = r_disk_buf->next;
    }

    if (r_disk_buf == pool->last) {
        pool->last = r_disk_buf->pre;
    }

    *disk_buf = r_disk_buf;
    return FS_ERR_OK;
}

/**
 * 释放前的检查
 * 检查缓存是否已经在内存中，或者有相同扇区的内存已经在内存中
 * @param pool
 * @param disk_buf
 * @return
 */
static xfat_err_t release_check(xdisk_buf_pool_t * pool, xdisk_buf_t * disk_buf) {
    xdisk_buf_t * curr = pool->first;

    while (curr) {
        if (curr == disk_buf) {
            return FS_ERR_EXISTED;
        }

        // todo: 考虑后续的扩展：以下可能是合理的情况，即同一扇区可能有不同线程分别申请缓存
        // 在释放时，就可能发生此种问题。目前来说，暂不支持多线程
        if ((curr->sector_no == disk_buf->sector_no)
            && (xdisk_buf_state(curr) != BUFFER_FLAG_FREE)) {
            return FS_ERR_EXISTED;
        }

        curr = curr->next;
    }
    return FS_ERR_OK;
}

/**
 * 释放已经分配出去的缓存
 * @param pool
 * @param disk_buf
 * @return
 */
xfat_err_t xdisk_buf_pool_release(xdisk_buf_pool_t * pool, xdisk_buf_t * disk_buf) {
    xfat_err_t err;

    if (disk_buf == (xdisk_buf_t *)0) {
        return FS_ERR_OK;
    }

    // 异常检查，临时测试添加的代码
    err = release_check(pool, disk_buf);
    if (err < 0) {
        return err;
    }

    // 最近使用的，放到队列头部
    disk_buf->next = pool->first;
    disk_buf->pre = (xdisk_buf_t *)0;

    if (pool->first == (xdisk_buf_t *)0) {
        pool->first = pool->last = disk_buf;
    } else {
        pool->first->pre = disk_buf;
        pool->first = disk_buf;
    }
    return FS_ERR_OK;
}

/**
 * 遍历整个缓存
 * @param pool
 * @return
 */
xfat_err_t xdisk_buf_pool_trans_acc(xdisk_buf_pool_t * pool, xdisk_buf_acc_t acc, void * arg) {
    xfat_err_t err;
    xdisk_buf_t * disk_buf = pool->first;

    while (disk_buf) {
        err = acc(disk_buf, arg);
        if (err < 0) {
            return err;
        }

        if (err == FS_ERR_EOF) {
            return err;
        }

        disk_buf = disk_buf->next;
    }
    return FS_ERR_OK;
}

