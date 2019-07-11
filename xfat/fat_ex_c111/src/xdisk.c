/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include "xfat.h"
#include "xdisk.h"

u8_t temp_buffer[512];

/**
 * 初始化磁盘设备
 * @param disk 初始化的设备
 * @param name 设备的名称
 * @return
 */
xfat_err_t xdisk_open(xdisk_t *disk, const char * name, xdisk_driver_t * driver, void * init_data) {
    xfat_err_t err;

    disk->driver = driver;

    // 底层驱动初始化
    err = disk->driver->open(disk, init_data);
    if (err < 0) {
        return err;
    }

    disk->name = name;
    return FS_ERR_OK;
}

/**
 * 关闭存储设备
 * @param disk
 * @return
 */
xfat_err_t xdisk_close(xdisk_t * disk) {
    xfat_err_t err;

    err = disk->driver->close(disk);
    if (err < 0) {
        return err;
    }

    return err;
}

/**
 * 从设备中读取指定扇区数量的数据
 * @param disk 读取的磁盘
 * @param buffer 读取数据存储的缓冲区
 * @param start_sector 读取的起始扇区
 * @param count 读取的扇区数量
 * @return
 */
xfat_err_t xdisk_read_sector(xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count) {
    xfat_err_t err;

    if (start_sector + count >= disk->total_sector) {
        return FS_ERR_PARAM;
    }

    err = disk->driver->read_sector(disk, buffer, start_sector, count);
    return err;
}

/**
 * 向设备中写指定的扇区数量的数据
 * @param disk 写入的存储设备
 * @param buffer 数据源缓冲区
 * @param start_sector 写入的起始扇区
 * @param count 写入的扇区数
 * @return
 */
xfat_err_t xdisk_write_sector(xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count) {
    xfat_err_t err;

    if (start_sector + count >= disk->total_sector) {
        return FS_ERR_PARAM;
    }

    err = disk->driver->write_sector(disk, buffer, start_sector, count);
    return err;
}

/**
 * 获取设备上总的分区数量
 * @param disk 查询的存储设备
 * @param count 分区数存储的位置
 * @return
 */
xfat_err_t xdisk_get_part_count(xdisk_t *disk, u32_t *count) {
	int r_count = 0, i = 0;
    mbr_part_t * part;
    u8_t * disk_buffer = temp_buffer;

    // 读取mbr区
	int err = xdisk_read_sector(disk, disk_buffer, 0, 1);
	if (err < 0) {
		return err;
	}

	// 解析统计主分区的数量，并标记出哪个分区是扩展分区
	part = ((mbr_t *)disk_buffer)->part_info;
	for (i = 0; i < MBR_PRIMARY_PART_NR; i++, part++) {
		if (part->system_id == FS_NOT_VALID) {
            continue;
        } else {
            r_count++;
        }
	}

    *count = r_count;
	return FS_ERR_OK;
}
