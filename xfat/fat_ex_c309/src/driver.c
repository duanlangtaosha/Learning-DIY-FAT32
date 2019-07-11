/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include <stdio.h>
#include <time.h>
#include "xdisk.h"
#include "xfat.h"

/**
 * 初始化磁盘设备
 * @param disk 初始化的设备
 * @param name 设备的名称
 * @return
 */
static xfat_err_t xdisk_hw_open(xdisk_t *disk, void * init_data) {
    const char * path = (const char *)init_data;

    FILE * file = fopen(path, "rb+");
    if (file == NULL) {
        printf("open disk failed:%s\n", path);
        return FS_ERR_IO;
    }

    disk->data = file;
    disk->sector_size = 512;

    fseek(file, 0, SEEK_END);
    disk->total_sector = ftell(file) / disk->sector_size;
    return FS_ERR_OK;
}

/**
 * 关闭存储设备
 * @param disk
 * @return
 */
static xfat_err_t xdisk_hw_close(xdisk_t * disk) {
    FILE * file = (FILE *)disk->data;
    fclose(file);

    return FS_ERR_OK;
}

/**
 * 从设备中读取指定扇区数量的数据
 * @param disk 读取的磁盘
 * @param buffer 读取数据存储的缓冲区
 * @param start_sector 读取的起始扇区
 * @param count 读取的扇区数量
 * @return
 */
static xfat_err_t xdisk_hw_read_sector(xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count) {
    u32_t offset = start_sector * disk->sector_size;
    FILE * file = (FILE *)disk->data;

    int err = fseek(file, offset, SEEK_SET);
    if (err == -1) {
        printf("seek disk failed:0x%x\n", (int)offset);
        return FS_ERR_IO;
    }

    err = (int)fread(buffer, disk->sector_size, count, file);
    if (err == -1) {
        printf("read disk failed:sector:%d, count:%d\n",(int)start_sector, (int)count);
        return FS_ERR_IO;
    }
    return FS_ERR_OK;
}

/**
 * 向设备中写指定的扇区数量的数据
 * @param disk 写入的存储设备
 * @param buffer 数据源缓冲区
 * @param start_sector 写入的起始扇区
 * @param count 写入的扇区数
 * @return
 */
static xfat_err_t xdisk_hw_write_sector(xdisk_t *disk, u8_t *buffer, u32_t start_sector, u32_t count) {
    u32_t offset = start_sector * disk->sector_size;
    FILE * file = (FILE *)disk->data;

    int err = fseek(file, offset, SEEK_SET);
    if (err == -1) {
        printf("seek disk failed: 0x%x\n", (int)offset);
        return FS_ERR_IO;
    }

    err = (int)fwrite(buffer, disk->sector_size, count, file);
    if (err == -1) {
        printf("write disk failed:sector:%d, count:%d\n",(int)start_sector, (int)count);
        return FS_ERR_IO;
    }

    // 刷新一下，即时写入
    fflush(file);
    return FS_ERR_OK;
}

/**
 * 获取当前时间
 * @param timeinfo 时间存储的数据区
 * @return
 */
static xfat_err_t xdisk_hw_curr_time(xdisk_t *disk, xfile_time_t *timeinfo) {
    time_t raw_time;
    struct tm * local_time;

    // 获取本地时间
    time(&raw_time);
    local_time = localtime(&raw_time);

    // 拷贝转换
    timeinfo->year = local_time->tm_year + 1900;
    timeinfo->month = local_time->tm_mon + 1;
    timeinfo->day = local_time->tm_mday;
    timeinfo->hour = local_time->tm_hour;
    timeinfo->minute = local_time->tm_min;
    timeinfo->second = local_time->tm_sec;

    return FS_ERR_OK;
}

/**
 * 虚拟磁盘驱动结构
 */
xdisk_driver_t vdisk_driver = {
    .open = xdisk_hw_open,
    .close = xdisk_hw_close,
    .read_sector = xdisk_hw_read_sector,
    .write_sector = xdisk_hw_write_sector,
    .curr_time = xdisk_hw_curr_time,
};