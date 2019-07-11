#include <stdio.h>
#include "xfat.h"
#include "xdisk.h"
#include "xdisk_buf.h"

/**
 * 检查是否是mbr分区格式
 * @param sector_buffer
 * @return
 */
static int is_mbr_fmt(xdisk_t *disk, u8_t * sector_buffer) {
    int i;
    u32_t total_sectors = 0;
    mbr_t * mbr = (mbr_t *)sector_buffer;

    if ((mbr->boot_sig[0] != 0x55) || (mbr->boot_sig[1] != 0xaa)) {
        return  0;
    }

    for (i = 0; i < MBR_PRIMARY_PART_NR; i++) {
        mbr_part_t * part = &mbr->part_info[i];

        if ((part->boot_active != BOOT_INACTIVE) && (part->boot_active != BOOT_ACTIVE)) {
            return 0;
        }

        if ((part->total_sectors >= disk->total_sector) || (part->relative_sectors >= disk->total_sector)) {
            return 0;
        }

        total_sectors += part->total_sectors;
        if (total_sectors >= disk->total_sector) {
            return 0;
        }
    }
    return 1;
}

/**
 * 分区分区格式
 * @param disk
 * @param part_fmt
 * @return
 */
static xfat_err_t get_part_fmt (xdisk_t *disk, part_fmt_t * part_fmt) {
    xfat_err_t err;
    xdisk_buf_t * disk_buf;

    err = xdisk_buf_read_sector(disk, &disk_buf, 0);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
    }

    if (is_mbr_fmt(disk, disk_buf->buf)) {
        *part_fmt = PART_FMT_MBR;
    } else {
        *part_fmt = PART_FMT_NONE;
    }

    xdisk_release_buf(disk, disk_buf);
    return FS_ERR_OK;
}

/**
 * 初始化磁盘设备
 * @param disk 初始化的设备
 * @param name 设备的名称
 * @return
 */
xfat_err_t xdisk_open(xdisk_t *disk, const char * name, xdisk_driver_t * driver,
                      void * init_data, u8_t * disk_buf, u32_t buf_size) {
    xfat_err_t err;
    part_fmt_t part_fmt;

    disk->driver = driver;

    // 底层驱动初始化
    err = disk->driver->open(disk, init_data);
    if (err < 0) {
        return err;
    }

    err = xdisk_buf_pool_init(&disk->buf_list, disk->sector_size, disk_buf, buf_size);
    if (err < 0) {
        return err;
    }

    // 检查分区格式
    err = get_part_fmt(disk, &part_fmt);
    if (err < 0) {
        return err;
    }
    disk->part_fmt = part_fmt;

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

    err = xdisk_flush_all(disk);
    if (err < 0) {
        return err;
    }

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
 * 获取当前时间
 * @param timeinfo 时间存储的数据区
 * @return
 */
xfat_err_t xdisk_curr_time(xdisk_t *disk, struct _xfile_time_t *timeinfo) {
    xfat_err_t err;

    err = disk->driver->curr_time(disk, timeinfo);
    return err;
}

/**
 * 获取扩展分区下的子分区数量
 * @param disk 扩展分区所在的存储设备
 * @param start_sector 扩展分区所在的起始扇区
 * @param count 查询得到的子分区数量
 * @return
 */
static xfat_err_t disk_get_extend_part_count(xdisk_t * disk, u32_t start_sector, u32_t * count) {
    int r_count = 0;

    u32_t ext_start_sector = start_sector;
    do {
        mbr_part_t * part;
        xdisk_buf_t * disk_buf;

        // 读取扩展分区的mbr
        xfat_err_t err = xdisk_buf_read_sector(disk, &disk_buf, start_sector);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }

        // 当前分区无效，立即退出
        part = ((mbr_t *)disk_buf->buf)->part_info;
        if (part->system_id == FS_NOT_VALID) {
            xdisk_release_buf(disk, disk_buf);
            break;
        }

        r_count++;

        // 没有后续分区, 立即退出
        if ((++part)->system_id != FS_EXTEND) {
            xdisk_release_buf(disk, disk_buf);
            break;
        }

        // 寻找下一分区
        start_sector = ext_start_sector + part->relative_sectors;

        xdisk_release_buf(disk, disk_buf);
    } while (1);

    *count = r_count;

    return FS_ERR_OK;
}

/**
 * 获取mbr上总的分区数量
 * @param disk 查询的存储设备
 * @param count 分区数存储的位置
 * @return
 */
static xfat_err_t mbr_get_part_count(xdisk_t *disk, u32_t *count) {
	int r_count = 0, i = 0;
    mbr_part_t * part;
    u8_t extend_part_flag = 0;
    u32_t start_sector[4];
    xdisk_buf_t * disk_buf;

    // 读取mbr区
    xfat_err_t err = xdisk_buf_read_sector(disk, &disk_buf, 0);
	if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
	}

	// 解析统计主分区的数量，并标记出哪个分区是扩展分区
	part = ((mbr_t *)disk_buf->buf)->part_info;
	for (i = 0; i < MBR_PRIMARY_PART_NR; i++, part++) {
		if (part->system_id == FS_NOT_VALID) {
            continue;
        } else if (part->system_id == FS_EXTEND) {
            start_sector[i] = part->relative_sectors;
            extend_part_flag |= 1 << i;
        } else {
            r_count++;
        }
	}

    xdisk_release_buf(disk, disk_buf);

	// 统计各个扩展分区下有多少个子分区
    if (extend_part_flag) {
        for (i = 0; i < MBR_PRIMARY_PART_NR; i++) {
            if (extend_part_flag & (1 << i)) {
                u32_t ext_count = 0;
                err = disk_get_extend_part_count(disk, start_sector[i], &ext_count);
                if (err < 0) {
                    return err;
                }

                r_count += ext_count;
            }
        }
    }

    *count = r_count;
	return FS_ERR_OK;
}

/**
 * 获取设备上总的分区数量
 * @param disk 查询的存储设备
 * @param count 分区数存储的位置
 * @return
 */
xfat_err_t xdisk_get_part_count(xdisk_t *disk, u32_t *count) {
    xfat_err_t err;

    if (disk->part_fmt == PART_FMT_MBR) {
        err = mbr_get_part_count(disk, count);
    } else {
        *count = 1;
        err = FS_ERR_OK;
    }

    return err;
}


/**
 * 获取扩展下分区信息
 * @param disk 查询的存储设备
 * @param disk_part 分区信息存储的位置
 * @param start_sector 扩展分区起始的绝对物理扇区
 * @param part_no 查询的分区号
 * @param count 该扩展分区下一共有多少个子分区
 * @return
 */
static xfat_err_t disk_get_extend_part(xdisk_t * disk, xdisk_part_t * disk_part,
                    u32_t start_sector, int part_no, u32_t * count) {
    u32_t r_count = 0;
    xfat_err_t err = FS_ERR_OK;

    // 遍历整个扩展分区
    u32_t ext_start_sector = start_sector;
    do {
        mbr_part_t * part;
        xdisk_buf_t * disk_buf;

        // 读取扩展分区的mbr
        err = xdisk_buf_read_sector(disk, &disk_buf, start_sector);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }

        part = ((mbr_t *)disk_buf->buf)->part_info;
        if (part->system_id == FS_NOT_VALID) {  // 当前分区无效，设置未找到, 返回
            xdisk_release_buf(disk, disk_buf);
            break;
        }

        // 找到指定的分区号，计算出分区的绝对位置信息
        if (++r_count == (part_no + 1)) {
            disk_part->type = part->system_id;
            disk_part->start_sector = start_sector + part->relative_sectors;
            disk_part->total_sector = part->total_sectors;
            disk_part->disk = disk;

            xdisk_release_buf(disk, disk_buf);
            break;
        }

        if ((++part)->system_id != FS_EXTEND) { // 无后续分区，设置未找到, 返回
            xdisk_release_buf(disk, disk_buf);
            break;
        }

        start_sector = ext_start_sector + part->relative_sectors;
        xdisk_release_buf(disk, disk_buf);
    } while (1);

    *count = r_count;
    return err;
}

/**
 * 获取mbr指定序号的分区信息
 * 注意，该操作依赖物理分区分配，如果设备的分区结构有变化，则序号也会改变，得到的结果不同
 * @param disk 存储设备
 * @param part 分区信息存储的位置
 * @param part_no 分区序号
 * @return
 */
static xfat_err_t mbr_get_part(xdisk_t *disk, xdisk_part_t *xdisk_part, int part_no) {
    int i;
    int curr_no = -1;
    mbr_part_t * mbr_part;
    xdisk_buf_t * disk_buf;
    xfat_err_t err;

	// 读取mbr
	err = xdisk_buf_read_sector(disk, &disk_buf, 0);
	if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
		return err;
	}

	// 遍历4个主分区描述
    mbr_part = ((mbr_t *)disk_buf->buf)->part_info;
	for (i = 0; i < MBR_PRIMARY_PART_NR; i++, mbr_part++) {
		if (mbr_part->system_id == FS_NOT_VALID) {
			continue;
        }

		// 如果是扩展分区，则进入查询子分区
		if (mbr_part->system_id == FS_EXTEND) {
            u32_t count = 0;

            // 读扩展分区也要用缓冲区，为节省缓冲区，这里先释放一下
            xdisk_release_buf(disk, disk_buf);

            err = disk_get_extend_part(disk, xdisk_part, mbr_part->relative_sectors, part_no - i, &count);
            if (err < 0) {      // 有错误
                return err;
            }

            // 未找到，增加计数
            curr_no += count;
            if (curr_no == part_no) {
                return FS_ERR_OK;
            }

            // 重新读取mbr
            err = xdisk_buf_read_sector(disk, &disk_buf, 0);
            if (err < 0) {
                return err;
            }
        } else {
		    // 在主分区中找到，复制信息
            if (++curr_no == part_no) {
                xdisk_part->type = mbr_part->system_id;
                xdisk_part->start_sector = mbr_part->relative_sectors;
                xdisk_part->total_sector = mbr_part->total_sectors;
                xdisk_part->disk = disk;

                xdisk_release_buf(disk, disk_buf);
                return FS_ERR_OK;
            }
        }
	}

    xdisk_release_buf(disk, disk_buf);
    return FS_ERR_NONE;
}

/**
 * 获取指定序号的分区信息
 * 注意，该操作依赖物理分区分配，如果设备的分区结构有变化，则序号也会改变，得到的结果不同
 * @param disk 存储设备
 * @param part 分区信息存储的位置
 * @param part_no 分区序号
 * @return
 */
xfat_err_t xdisk_get_part(xdisk_t *disk, xdisk_part_t *xdisk_part, int part_no) {
    xfat_err_t err;

    if (disk->part_fmt == PART_FMT_MBR) {
        err = mbr_get_part(disk, xdisk_part, part_no);
    } else {
        if (part_no >= 1) {
            return FS_ERR_PARAM;
        }

        xdisk_part->type = FS_NOT_VALID;
        xdisk_part->start_sector = 0;
        xdisk_part->total_sector = disk->total_sector;
        xdisk_part->disk = disk;
        err = FS_ERR_OK;
    }

    return err;
}

/**
 * 以缓冲方式读取磁盘的指定扇区
 * @param disk
 * @param disk_buf
 * @param sector_no
 * @return
 */
xfat_err_t xdisk_buf_read_sector(xdisk_t *disk, xdisk_buf_t ** disk_buf, u32_t sector_no) {
    xfat_err_t err;
    xdisk_buf_t * r_disk_buf;
    int need_read = 0;

    err = xdisk_buf_pool_alloc(&disk->buf_list, BUFFER_FLAG_TYPE_SEC, sector_no, &r_disk_buf);
    if (err < 0) {
        return err;
    }

    switch (xdisk_buf_state(r_disk_buf)) {
    case BUFFER_FLAG_FREE:
        need_read = 1;  // 空闲块，要重读
        break;
    case BUFFER_FLAG_CLEAN:
        need_read = sector_no != r_disk_buf->sector_no;
        break;
    case BUFFER_FLAG_DIRTY:
        // 其它块的脏数据，需要回写，再重读. 如果是本块，继续使用即可
       if (sector_no != r_disk_buf->sector_no) {
            err = xdisk_write_sector(disk, r_disk_buf->buf, r_disk_buf->sector_no, 1);
            if (err < 0) {
                xdisk_release_buf(disk, r_disk_buf);
                return err;
            }

            need_read = 1;
            xdisk_buf_set_state(r_disk_buf, BUFFER_FLAG_FREE);
       }
        break;
    default:
        break;
    }

    if (need_read) {
        err = xdisk_read_sector(disk, r_disk_buf->buf, sector_no, 1);
        if (err < 0) {
            xdisk_release_buf(disk, r_disk_buf);
            return err;
        }
        xdisk_buf_set_state(r_disk_buf, BUFFER_FLAG_CLEAN);
    }

    xdisk_buf_set_type(r_disk_buf, BUFFER_FLAG_TYPE_SEC);
    r_disk_buf->sector_no = sector_no;

    *disk_buf = r_disk_buf;
    return FS_ERR_OK;
}

/**
 * 以缓冲方式写取磁盘的指定扇区
 * @param disk
 * @param disk_buf
 * @param sector_no
 * @return
 */
xfat_err_t xdisk_buf_write_sector(xdisk_t *disk, xdisk_buf_t * disk_buf, u32_t sector_no) {
    xfat_err_t err;

    // 缓存写，留空
    // 后续扩展：如果是写穿透，则此处再开始写，同时清除缓冲脏标记

    // 如果是工作缓存，直接写
    if (disk_buf->flags & BUFFER_FLAG_TYPE_WORK) {
        err = xdisk_write_sector(disk, disk_buf->buf, sector_no, 1);
        if (err < 0) {
            return err;
        }

        // 设置为最后写入的扇区号
        disk_buf->sector_no = sector_no;
        xdisk_buf_set_state(disk_buf, BUFFER_FLAG_CLEAN);
    } else {
        xdisk_buf_set_state(disk_buf, BUFFER_FLAG_DIRTY);
    }

    return FS_ERR_OK;
}

/**
 * 为工作缓存分配空间
 * @param disk
 * @param disk_buf
 * @return
 */
xfat_err_t xdisk_alloc_working_buf(xdisk_t * disk, xdisk_buf_t ** disk_buf) {
    xfat_err_t err;
    xdisk_buf_t * r_disk_buf;

    // 工作缓存，不指向特定扇区
    err = xdisk_buf_pool_alloc(&disk->buf_list, BUFFER_FLAG_TYPE_WORK, 0, &r_disk_buf);
    if (err < 0) {
        return err;
    }

    if (xdisk_buf_state(r_disk_buf) == BUFFER_FLAG_DIRTY) {
        err = xdisk_write_sector(disk, r_disk_buf->buf, r_disk_buf->sector_no, 1);
        if (err < 0) {
            return err;
        }
    }

    xdisk_buf_set_state(r_disk_buf, BUFFER_FLAG_CLEAN);
    xdisk_buf_set_type(r_disk_buf, BUFFER_FLAG_TYPE_WORK);
    r_disk_buf->sector_no = 0;

    *disk_buf = r_disk_buf;
    return FS_ERR_OK;
}

/**
 * 释放已经分配的磁盘缓存
 * @param disk
 * @param disk_buf
 * @return
 */
xfat_err_t xdisk_release_buf(xdisk_t *disk, xdisk_buf_t * disk_buf) {
    xfat_err_t err = xdisk_buf_pool_release(&disk->buf_list, disk_buf);
    return err;
}

/**
 * 缓存刷新回调函数
 * @param disk_buf
 * @param arg
 * @return
 */
static xfat_err_t buf_flush_acc (xdisk_buf_t * disk_buf, void * arg) {
    xdisk_t * disk = (xdisk_t *)arg;
    xfat_err_t err = FS_ERR_OK;

    switch (disk_buf->flags & BUFFER_FLAG_STATE_MSK) {
    case BUFFER_FLAG_FREE:
        return FS_ERR_OK;
    case BUFFER_FLAG_CLEAN:
        break;
    case BUFFER_FLAG_DIRTY:
        err = xdisk_write_sector(disk, disk_buf->buf, disk_buf->sector_no, 1);
        if (err < 0) {
            return err;
        }
        xdisk_buf_set_state(disk_buf, BUFFER_FLAG_CLEAN);
        break;
    }

    return err;
}

/**
 * 回与缓冲区中数据块
 * @param disk
 * @return
 */
xfat_err_t xdisk_flush_all(xdisk_t * disk) {
    xfat_err_t err;

    err = xdisk_buf_pool_trans_acc(&disk->buf_list, buf_flush_acc, disk);
    if (err < 0) {
        return err;
    }

    return FS_ERR_OK;
}

