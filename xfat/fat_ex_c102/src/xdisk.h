/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#ifndef XDISK_H
#define	XDISK_H

#include "xtypes.h"

// 相关前置声明
struct _xdisk_t;

/**
 * 磁盘驱动接口
 */
typedef struct _xdisk_driver_t {
    xfat_err_t (*open) (struct _xdisk_t * disk, void * init_data);
    xfat_err_t (*close) (struct _xdisk_t * disk);
    xfat_err_t (*read_sector) (struct _xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count);
    xfat_err_t (*write_sector) (struct _xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count);
}xdisk_driver_t;

/**
 * 存储设备类型
 */
typedef struct _xdisk_t {
    u32_t sector_size;              // 块大小
	u32_t total_sector;             // 总的块数量
    xdisk_driver_t * driver;        // 驱动接口
    void * data;                    // 设备自定义参数
}xdisk_t;

#endif

