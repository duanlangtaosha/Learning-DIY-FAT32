#include "xfat.h"
#include "xdisk.h"

#define XFAT_MB(n)  ((n) * 1024 * 1024L)
#define XFAT_GB(n)  ((n) * 1024 * 1024 * 1024LL)xfat_mount

#define XFAT_MAX(a, b) ((a) > (b) ? (a) : (b))
 
// 内置的.和..文件名             "12345678ext"
#define DOT_FILE                ".          "
#define DOT_DOT_FILE            "..         "

#define is_path_sep(ch)         (((ch) == '\\') || ((ch == '/')))       // 判断是否是文件名分隔符
#define is_path_end(path)       (((path) == 0) || (*path == '\0'))      // 判断路径是否为空
#define file_get_disk(file)     ((file)->xfat->disk_part->disk)         // 获取disk结构
#define xfat_get_disk(xfat)     ((xfat)->disk_part->disk)               // 获取disk结构
#define to_sector(disk, offset)     ((offset) / (disk)->sector_size)    // 将依稀转换为扇区号
#define to_sector_offset(disk, offset)   ((offset) % (disk)->sector_size)   // 获取扇区中的相对偏移
#define to_sector_addr(disk, offset)    ((offset) / (disk)->sector_size * (disk)->sector_size)  // 取Offset所在扇区起始地址
#define to_cluster_offset(xfat, pos)      ((pos) % ((xfat)->cluster_byte_size)) // 获取簇中的相对偏移
#define to_phy_sector(xfat, cluster, offset)  cluster_fist_sector((xfat), (cluster)) + to_sector((disk), (offset));

static xfat_t * xfat_list;          // 已挂载的xfat链表

/**
 * 初始化xfat挂载链表
 */
void xfat_list_init (void) {
    xfat_list = (xfat_t *)0;
}

/**
 * 添加xfat到链表中
 * @param xfat 待添加的xfat
 */
void xfat_list_add (xfat_t * xfat) {
    if (xfat_list == (xfat_t *)0) {
        xfat_list = xfat;
    } else {
        xfat->next = xfat_list;
        xfat_list = xfat;
    }
}

/**
 * 将指定的xfat从链表中移除
 * @param xfat 待移除的xfat
 */
void xfat_list_remove (xfat_t * xfat) {
    xfat_t * pre = (xfat_t *)0;
    xfat_t * curr = xfat_list;

    // 遍历找到设备结点
    while ((curr != xfat) && (curr != (xfat_t *)0)) {
        pre = curr;
        curr = curr->next;
    }

    // 解除链接
    if (curr == xfat) {
        if (curr == xfat_list) {
            xfat_list = curr->next;
        } else {
            pre->next = curr->next;
        }
        curr->next = (xfat_t *)0;
    }
}

/**
 * 检查挂载名是否与xfat中的名称相同
 * @param name 待检查的挂载名
 * @return
 */
static int is_mount_name_match (xfat_t * xfat, const char * name) {
    const char * s = xfat->name, * d = name;

    // 跳过开头的'/'或者'\'
    while (is_path_sep(*d)) {
        d++;
    }

    // 解析名称，名称要完全相同才认为是相同
    while ((*s != '\0') && (*d != '\0')) {
        // 未遍历完即遇到分隔符，退出
        if (is_path_sep(*d)) {
            return 0;
        }

        // 字符不相同，退出
        if (*s++ != *d++) {
            return 0;
        }
    }

    return  !((*s != '\0') || ((*d != '\0') && !is_path_sep(*d)));
}

/**
 * 根据名称查询xfat结构
 * @param name xfat的名称
 * @return 查找到的xfat
 */
static xfat_t * xfat_find_by_name (const char * name) {
    xfat_t * curr = xfat_list;

    // 遍历找到设备结点
    while (curr != (xfat_t *)0) {
        if (is_mount_name_match(curr, name)) {
            return curr;
        }

        curr = curr->next;
    }

    return (xfat_t *)0;
}

/**
 * 初始化xfat文件系统
 * @return
 */
xfat_err_t xfat_init(void) {
    xfat_list_init();
    return FS_ERR_OK;
}

/**
 * 从dbr中解析出fat相关配置参数
 * @param dbr 读取的设备dbr
 * @return
 */
static xfat_err_t parse_fat_header (xfat_t * xfat, dbr_t * dbr) {
    xdisk_part_t * xdisk_part = xfat->disk_part;

    // 解析DBR参数，解析出有用的参数
    xfat->root_cluster = dbr->fat32.BPB_RootClus;
    xfat->fat_tbl_sectors = dbr->fat32.BPB_FATSz32;

    // 如果禁止FAT镜像，只刷新一个FAT表
    // disk_part->start_block为该分区的绝对物理扇区号，所以不需要再加上Hidden_sector
    if (dbr->fat32.BPB_ExtFlags & (1 << 7)) {
        u32_t table = dbr->fat32.BPB_ExtFlags & 0xF;
        xfat->fat_start_sector = dbr->bpb.BPB_RsvdSecCnt + xdisk_part->start_sector + table * xfat->fat_tbl_sectors;
        xfat->fat_tbl_nr = 1;
    } else {
        xfat->fat_start_sector = dbr->bpb.BPB_RsvdSecCnt + xdisk_part->start_sector;
        xfat->fat_tbl_nr = dbr->bpb.BPB_NumFATs;
    }

    xfat->sec_per_cluster = dbr->bpb.BPB_SecPerClus;
    xfat->total_sectors = dbr->bpb.BPB_TotSec32;
    xfat->cluster_byte_size = xfat->sec_per_cluster * dbr->bpb.BPB_BytsPerSec;

    return FS_ERR_OK;
}

/**
 * 将xfat添加至指定的挂载点
 * @param xfat xfat结构
 * @param mount_name 挂载点名称
 * @return
 */
xfat_err_t add_to_mount(xfat_t * xfat, const char * mount_name) {
    memset(xfat->name, 0, XFAT_NAME_LEN);
    strncpy(xfat->name, mount_name, XFAT_NAME_LEN);
    xfat->name[XFAT_NAME_LEN - 1] = '\0';

    // 检查是否已经存在同名的挂载点
    if (xfat_find_by_name(xfat->name)) {
        return FS_ERR_EXISTED;
    }

    xfat_list_add(xfat);
    return FS_ERR_OK;
}

/**
 * 检查文件系统类型
 * @param part
 * @return
 */
static int is_fs_type_legal(xdisk_part_t * part) {
    // 以下无论是哪种分区格式，还需要再次检查是否是FAT32格式
    // 作为练习实现!
    if (part->disk->part_fmt == PART_FMT_NONE) {
        return 1;
    }

    switch (part->type) {
    case FS_FAT32:
    case FS_WIN95_FAT32_0:
    case FS_WIN95_FAT32_1:
        return 1;
    default:
        break;
    }

    return 0;
}

/**
 * 获取xfat的总簇数
 * @param xfat
 * @return
 */
u32_t total_clusters(xfat_t * xfat) {
    xdisk_t * disk = xfat_get_disk(xfat);

    return xfat->fat_tbl_sectors * disk->sector_size / sizeof(cluster32_t);
}

/**
 * 将簇号转为FAT表中的扇区偏移
 * @param xfat
 * @param cluster 
 * @return
 */
static u32_t cluster_to_fat_sector(xfat_t * xfat, u32_t cluster) {
    u32_t sector;
    xdisk_t * disk = xfat_get_disk(xfat);

    sector = (cluster * CLUSTER_WIDTH + disk->sector_size - 1) / disk->sector_size;
    return sector;
}

/**
 * 从磁盘加载空闲cluster给fat
 * 最后实际查找到的数量可能比要求的要少
 * @param xfat
 * @param request_count 申请加载的数量，如果为0，则尽可能加载越多越好
 * @return
 */
static xfat_err_t load_clusters_for_xfat(xfat_t * xfat, u32_t request_count) {
    xfat_err_t err;
    u32_t  i;
    u32_t search_sector;
    u32_t pre_sector = 0;
    xfat_buf_t * disk_buf = (xfat_buf_t *)0;
    u32_t curr_cluster, begin_cluster, end_cluster;
    u32_t free_count;
    xdisk_t * disk = xfat_get_disk(xfat);

    if (xcluster_chain_count(&xfat->free_chain) == 0) {
        // 空闲簇链为空，从第1个可用扇区开始查找
        search_sector = xfat->fat_start_sector;
        curr_cluster = 0;
    } else {
        // 从最后一项的下一位置开始查找
        // 最后一项可能不是整个簇链最大的簇区间
        xcluster_item_t * item = xcluster_chain_last_item(&xfat->free_chain);
        u32_t next_cluster = xcluster_item_end(item) + 1;
        if (next_cluster >= total_clusters(xfat)) { // 超出边界
            search_sector = xfat->fat_start_sector;
            curr_cluster = 0;
        } else {
            search_sector = xfat->fat_start_sector + cluster_to_fat_sector(xfat, next_cluster);
            curr_cluster = next_cluster;
        }
    }

    // 无论从哪个位置开始找，都要遍历整个FAT表
    begin_cluster = end_cluster = CLUSTER_INVALID;
    free_count = 0;
    for (i = 0; i < xfat->fat_tbl_sectors; i++, search_sector++) {
        u32_t j;
        cluster32_t * cluster32;

        // 搜索可能不是从表头开始，注意回绕
        if (search_sector >= (xfat->fat_start_sector + xfat->fat_tbl_sectors)) {
            search_sector = xfat->fat_start_sector;
        }

        // 当超过读取数量，结束查找
        // 之所以不放在下面的for中，是希望加载够时，还能继续加载同一扇区的其余簇
        if (request_count && free_count >= request_count) {
            if (is_cluster_valid(begin_cluster)) {
                xcluster_item_t item;
                xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                xcluster_chain_add(&xfat->free_chain, &item, 0);
            }
            xdisk_release_buf(disk, disk_buf);
            return FS_ERR_OK;
        }

        // todo: 这里可以再优化，即判断某扇区是否已经在簇列表中，这样就不必再重新加载搜索
        // 留待以后实现
        //if (sector_in_cluseter_chain(&xfat->cluster_chain, search_sector)) {
        //    // 刚好扇区边界是结束簇
        //    if (is_cluster_valid(begin_cluster)) {
        //        xcluster_item_t item;
        //        xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
        //        xcluster_chain_add(&xfat->cluster_chain, &item, 0);

        //        begin_cluster = end_cluster = CLUSTER_INVALID;
        //    }

        //    curr_cluster += disk->sector_size / CLUSTER_WIDTH;
        //    continue;
        //}

        // 加载扇区数据
        err = xfat_bpool_read_sector(disk, &disk_buf, search_sector);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }

        // 逐个解析FAT表项，以下只适用于FAT32
        cluster32 = (cluster32_t *)disk_buf->buf;
        for (j = 0; j < disk->sector_size; j += CLUSTER_WIDTH, cluster32++, curr_cluster++) {
            // 无空闲位置存放
            if (xcluster_chain_free_count(&xfat->free_chain) == 0) {
                if (begin_cluster != CLUSTER_INVALID) {
                    // 数量够，结束搜索
                    xcluster_item_t item;
                    xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                    xcluster_chain_add(&xfat->free_chain, &item, 1);
                }

                xdisk_release_buf(disk, disk_buf);
                return FS_ERR_OK;
            }

            if (cluster32->v == CLUSTER_FREE) {
                // 遇到空闲，增加计数
                // 如果是第1个非空闲，则为开始簇
                ++free_count;

                if (begin_cluster == CLUSTER_INVALID) {
                    begin_cluster = curr_cluster;
                    end_cluster = curr_cluster;
                } else if ((end_cluster + 1) == curr_cluster) {
                    end_cluster++;
                } else {
                    // 不相邻簇， 使用新区间
                    xcluster_item_t item;
                    xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                    xcluster_chain_add(&xfat->free_chain, &item, 0);

                    begin_cluster = end_cluster = CLUSTER_INVALID;
                }
            } else if (begin_cluster != CLUSTER_INVALID) {
                // 结束一个簇区间
                xcluster_item_t item;
                xcluster_item_init(&item, begin_cluster, curr_cluster - 1, XCLUSTER_FLAG_CLEAN);
                xcluster_chain_add(&xfat->free_chain, &item, 1);

                // 启动下一簇区间的查找
                begin_cluster = end_cluster = CLUSTER_INVALID;
            }
        }

        xdisk_release_buf(disk, disk_buf);
    }

    return FS_ERR_OK;
}

/**
 * 初始化FAT项
 * @param xfat xfat结构
 * @param disk_part 分区结构
 * @return
 */
xfat_err_t xfat_mount(xfat_t * xfat, xdisk_part_t * xdisk_part, const char * mount_name) {
    dbr_t * dbr;
    xdisk_t * xdisk = xdisk_part->disk;
    xfat_err_t err;
    xfat_buf_t * disk_buf;

    if (!is_fs_type_legal(xdisk_part)) {
        return FS_ERR_INVALID_FS;
    }

    xfat->disk_part = xdisk_part;

    // 读取dbr参数区
    err = xfat_bpool_read_sector(xdisk, &disk_buf, xdisk_part->start_sector);
    if (err < 0) {
        return err;
    }
    dbr = (dbr_t *)disk_buf->buf;

    // 解析dbr参数中的fat相关信息
    err = parse_fat_header(xfat, dbr);
    if (err < 0) {
        return err;
    }

    // 释放之前的缓存
    xdisk_release_buf(xdisk, disk_buf);

    // 簇项初始化
    xcluster_chain_init(&xfat->free_chain, xfat->free_items, XFAT_CLUSTER_FREE_CHAIN_SIZE);
    load_clusters_for_xfat(xfat, XFAT_CLUSTER_PRELOAD_SIZE);        // 挂载时，加载越多越好？
    xcluster_chain_init(&xfat->work_chain, xfat->work_items, XFAT_CLUSTER_WORK_CHAIN_SIZE);

    // 先一次性全部读取FAT表: todo: 优化
    xfat->fat_buffer = (u8_t *)malloc(xfat->fat_tbl_sectors * xdisk->sector_size);
    err = xdisk_read_sector(xdisk, (u8_t *)xfat->fat_buffer, xfat->fat_start_sector, xfat->fat_tbl_sectors);
    if (err < 0) {
        return err;
    }

    // 添加至挂载点
    err = add_to_mount(xfat, mount_name);
    return err;
}

/**
 * 解除文件系统的挂载
 * @param xfat
 */
void xfat_unmount(xfat_t * xfat) {
    xfat_list_remove(xfat);
}

/**
 * 初始化格式化参数，以给一个初始的缺省值
 * @param ctrl 配置结构
 * @return
 */
xfat_err_t xfat_fmt_ctrl_init(xfat_fmt_ctrl_t * ctrl) {
    xfat_err_t err = FS_ERR_OK;

    ctrl->type = FS_FAT32;
    ctrl->cluster_size = XFAT_CLUSTER_AUTO;
    return err;
}

/**
 * 关闭已经打开的文件
 * @param file 待关闭的文件
 * @return
 */
xfat_err_t xfile_close(xfile_t *file) {
    // 在这里，更新文件的访问时间，写时间和扩容和的增量，属性值的修改等

    xfat_err_t err = xfile_flush(file);
    if (err < 0) {
        return err;
    }
    return FS_ERR_OK;
}

/**
 * 回写文件缓存
 * @param file 
 * @return
 */
xfat_err_t xfile_flush(xfile_t * file) {
    // 暂时刷新整个磁盘，后期再考虑刷新到单个扇区，或者定位到特定的文件数据区
    xdisk_flush_all(file_get_disk(file));
    return FS_ERR_OK;
}

static xfat_err_t create_vol_id_label(xdisk_t * disk, dbr_t * dbr) {
    xfat_err_t err;
    xfile_time_t time;
    u32_t ms;

    err = xdisk_curr_time(disk, &time);
    if (err < 0) {
        return err;
    }

    ms = (time.year << 7) | (time.month << 4) | (time.day << 5)
         | (time.hour << 0) | (time.minute << 0) | (time.second << 0)
         | (time.mil_second << 0);

    dbr->fat32.BS_VolID = ms;
    memcpy(dbr->fat32.BS_VolLab, "NO NAME    ", 11);

    return FS_ERR_OK;
}

/**
 * 计算所需要的fat表扇区数
 * @param dbr dbr分区参数
 * @param xdisk_part 分区信息
 * @param ctrl 格式化控制参数
 * @return 每个fat表项大小
 */
static u32_t cal_fat_tbl_sectors (dbr_t * dbr, xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl) {
    // 保留区 + fat表数量 * fat表扇区数 + 数据区扇区数 = 总扇区数
    // fat表扇区数 * 扇区大小 / 4（fat32表项） = 数据区扇区数 / 每簇扇区数
    // 计算得到：fat表扇区数=(总扇区数-保留区扇区数)/(FAT表项大小+块大小*每簇扇区数/4)
    u32_t sector_size = xdisk_part->disk->sector_size;
    u32_t fat_dat_sectors = xdisk_part->total_sector - dbr->bpb.BPB_RsvdSecCnt;
    u32_t fat_sector_count = fat_dat_sectors / (dbr->bpb.BPB_NumFATs + sector_size * dbr->bpb.BPB_SecPerClus / 4);

    return fat_sector_count;
}

/**
 * 获取默认的簇大小
 * @param xdisk_part 待格式化的分区
 * @param xfs_type 分区类型
 * @return
 */
static u32_t get_default_cluster_size (xdisk_part_t * xdisk_part, xfs_type_t xfs_type) {
    u32_t sector_size = xdisk_part->disk->sector_size;
    u64_t part_size = xdisk_part->total_sector * sector_size;
    u32_t cluster_size;

    if (part_size <= XFAT_MB(64)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_512B, sector_size);
    } else if (part_size <= XFAT_MB(128)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_1K, sector_size);
    } else if (part_size <= XFAT_MB(256)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_2K, sector_size);
    } else if (part_size <= XFAT_GB(8)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_4K, sector_size);
    } else if (part_size <= XFAT_GB(16)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_8K, sector_size);
    } else if (part_size <= XFAT_GB(32)) {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_16K, sector_size);
    } else {
        cluster_size = XFAT_MAX(XFAT_CLUSTER_32K, sector_size);
    }
    return cluster_size;
}

/**
 * 根据分区及格式化参数，创建dbr头
 * @param dbr dbr头结构
 * @param xdisk_part 分区结构
 * @param ctrl 格式化参数
 * @return
 */
static xfat_err_t create_dbr (xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl, xfat_fmt_info_t * fmt_info) {
    xfat_err_t err;
    xdisk_t * disk = xdisk_part->disk;
    u32_t cluster_size;
    dbr_t * dbr;
    xfat_buf_t * disk_buf;

    // 分配临时缓存
    err = xdisk_alloc_working_buf(xdisk_part->disk, &disk_buf);
    if (err < 0) {
        return err;
    }
    dbr = (dbr_t *)disk_buf->buf;

    // 计算簇大小，簇大小不能比扇区大小要小
    if (ctrl->cluster_size < disk->sector_size) {
        xdisk_release_buf(xdisk_part->disk, disk_buf);
        return FS_ERR_PARAM;
    } else if (ctrl->cluster_size == XFAT_CLUSTER_AUTO) {
        cluster_size = get_default_cluster_size(xdisk_part, ctrl->type);
    } else {
        cluster_size = ctrl->cluster_size;
    }

    memset(dbr, 0, disk->sector_size);
    dbr->bpb.BS_jmpBoot[0] = 0xEB;          // 这几个跳转代码是必须的
    dbr->bpb.BS_jmpBoot[1] = 0x58;          // 不加win会识别为未格式化
    dbr->bpb.BS_jmpBoot[2] = 0x00;
    strncpy((char *)dbr->bpb.BS_OEMName, "XFAT SYS", 8);
    dbr->bpb.BPB_BytsPerSec = disk->sector_size;
    dbr->bpb.BPB_SecPerClus = to_sector(disk, cluster_size);
    dbr->bpb.BPB_RsvdSecCnt = 38;           // 固定值为32
    dbr->bpb.BPB_NumFATs = 2;               // 固定为2
    dbr->bpb.BPB_RootEntCnt = 0;            // FAT32未用
    dbr->bpb.BPB_TotSec16 = 0;              // FAT32未用
    dbr->bpb.BPB_Media = 0xF8;              // 固定值
    dbr->bpb.BPB_FATSz16 = 0;               // FAT32未用
    dbr->bpb.BPB_SecPerTrk = 0xFFFF;        // 不支持硬盘结构
    dbr->bpb.BPB_NumHeads = 0xFFFF;         // 不支持硬盘结构
    dbr->bpb.BPB_HiddSec = xdisk_part->start_sector;    // 是否正确?
    dbr->bpb.BPB_TotSec32 = xdisk_part->total_sector;

    dbr->fat32.BPB_FATSz32 = cal_fat_tbl_sectors(dbr, xdisk_part, ctrl);
    dbr->fat32.BPB_ExtFlags = 0;            // 固定值，实时镜像所有FAT表
    dbr->fat32.BPB_FSVer = 0;               // 版本号，0
    dbr->fat32.BPB_RootClus = 2;            // 固定为2，如果为坏簇怎么办？
    dbr->fat32.BPB_FsInfo = 1;              // fsInfo的扇区号

    memset(dbr->fat32.BPB_Reserved, 0, 12);
    dbr->fat32.BS_DrvNum = 0x80;            // 固定为0
    dbr->fat32.BS_Reserved1 = 0;
    dbr->fat32.BS_BootSig = 0x29;           // 固定0x29
    err = create_vol_id_label(disk, dbr);
    if (err < 0) {
        xdisk_release_buf(xdisk_part->disk, disk_buf);
        return err;
    }
    memcpy(dbr->fat32.BS_FileSysType, "FAT32   ", 8);

    disk_buf->buf[510] = 0x55;
    disk_buf->buf[511] = 0xAA;

    err = xfat_bpool_write_sector(disk, disk_buf, xdisk_part->start_sector);
    if (err < 0) {
        xdisk_release_buf(xdisk_part->disk, disk_buf);
        return err;
    }

    // 同时在备份区中写一个备份
    err = xfat_bpool_write_sector(disk, disk_buf, xdisk_part->start_sector + dbr->fat32.BPB_BkBootSec);
    if (err < 0) {
        xdisk_release_buf(xdisk_part->disk, disk_buf);
        return err;
    }

    xdisk_release_buf(xdisk_part->disk, disk_buf);

    // 提取格式化相关的参数信息，避免占用内部缓冲区
    fmt_info->fat_count = dbr->bpb.BPB_NumFATs;
    fmt_info->media = dbr->bpb.BPB_Media;
    fmt_info->fat_sectors = dbr->fat32.BPB_FATSz32;
    fmt_info->rsvd_sectors = dbr->bpb.BPB_RsvdSecCnt;
    fmt_info->root_cluster = dbr->fat32.BPB_RootClus;
    fmt_info->sec_per_cluster = dbr->bpb.BPB_SecPerClus;
    fmt_info->backup_sector = dbr->fat32.BPB_BkBootSec;
    fmt_info->fsinfo_sector = dbr->fat32.BPB_FsInfo;
    return err;
}

/**
 * 检查是否支持指定的文件系统
 */
int xfat_is_fs_supported(xfs_type_t type) {
    switch (type) {
        case FS_FAT32:
        case FS_WIN95_FAT32_0:
        case FS_WIN95_FAT32_1:
            return 1;
        default:
            return 0;
    }
}

/**
 * 格式化FAT表
 * @param dbr db结构
 * @param xdisk_part 分区信息
 * @param ctrl 格式化参数
 * @return
 */
static xfat_err_t create_fat_table (xfat_fmt_info_t * fmt_info, xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl) {
    int i, j;
    xdisk_t * disk = xdisk_part->disk;
    cluster32_t * fat_buffer;
    xfat_err_t err = FS_ERR_OK;
    u32_t fat_start_sector = fmt_info->rsvd_sectors + xdisk_part->start_sector;
    xfat_buf_t * disk_buf;

    // 分配临时缓存
    err = xdisk_alloc_working_buf(disk, &disk_buf);     // todo: 调整扇区号
    if (err < 0) {
        return err;
    }
    fat_buffer = (cluster32_t *)disk_buf->buf;

    // 逐个写多个FAT表
    memset(fat_buffer, 0, disk->sector_size);
    for (i = 0; i < fmt_info->fat_count; i++) {
        u32_t start_sector = fat_start_sector + fmt_info->fat_sectors * i;

        // 每个FAT表的前1、2簇已经被占用, 簇2分配给根目录，保留
        fat_buffer[0].v = (u32_t)(0x0FFFFF00 | fmt_info->media);
        fat_buffer[1].v = 0x0FFFFFFF;
        fat_buffer[2].v = 0x0FFFFFFF;
        err = xfat_bpool_write_sector(disk, disk_buf, start_sector++);
        if (err  < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }

        // 再写其余扇区的簇
        fat_buffer[0].v = fat_buffer[1].v = fat_buffer[2].v = 0;
        for (j = 1; j < fmt_info->fat_sectors; j++) {
            err = xfat_bpool_write_sector(disk, disk_buf, start_sector++);
            if (err  < 0) {
                xdisk_release_buf(disk, disk_buf);
                return err;
            }
        }
    }
 
    xdisk_release_buf(disk, disk_buf);
    return err;
}

/**
 * 创建根目录结构
 * @param dbr dbr结构
 * @param xdisk_part 分区信息
 * @param ctrl 格式控制参数
 * @return
 */
static xfat_err_t create_root_dir(xfat_fmt_info_t * fmt_info, xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl) {
    xfat_err_t err;
    int i;
    u32_t sector;
    xdisk_t * xdisk = xdisk_part->disk;
    u32_t data_sector = fmt_info->rsvd_sectors             // 保留区
            + (fmt_info->fat_count * fmt_info->fat_sectors)  // FAT区
            + (fmt_info->root_cluster - 2) * fmt_info->sec_per_cluster;     // 数据区
    diritem_t * diritem;
    xfat_buf_t * disk_buf;

    err = xdisk_alloc_working_buf(xdisk, &disk_buf);
    if (err < 0) {
        return err;
    }
    diritem = (diritem_t *)disk_buf->buf;

    // 擦除根目录所在的簇
    memset(disk_buf->buf, 0, xdisk->sector_size);
    sector = xdisk_part->start_sector + data_sector;
    for (i = 0; i < fmt_info->sec_per_cluster; i++) {
        err = xfat_bpool_write_sector(xdisk, disk_buf, sector + i);
        if (err < 0) {
            xdisk_release_buf(xdisk, disk_buf);
            return err;
        }
    }

    // 创建卷标和.目录
    diritem_init_default(diritem, xdisk, 0, ctrl->vol_name ? ctrl->vol_name : "DISK", 0);
    diritem->DIR_Attr |= DIRITEM_ATTR_VOLUME_ID;

    // 写入相关文件信息
    err = xfat_bpool_write_sector(xdisk_part->disk, disk_buf, xdisk_part->start_sector + data_sector);
    if (err < 0) {
        xdisk_release_buf(xdisk, disk_buf);
        return err;
    }

    xdisk_release_buf(xdisk, disk_buf);
    return err;
}

/**
 * 创建fsinfo区
 * @param dbr db结构
 * @param xdisk_part 分区信息
 * @param ctrl 格式化参数
 * @return
 */
static xfat_err_t create_fsinfo(xfat_fmt_info_t * fmt_info, xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl) {
    xfat_err_t err;
    fsinto_t * fsinfo;
    xdisk_t * disk = xdisk_part->disk;
    u32_t fsinfo_sector = xdisk_part->start_sector + fmt_info->fsinfo_sector;
    xfat_buf_t * disk_buf;

    // 分配临时缓存
    err = xdisk_alloc_working_buf(disk, &disk_buf);
    if (err < 0) {
        return err;
    }
    fsinfo = (fsinto_t *)disk_buf->buf;

    memset(fsinfo, 0, sizeof(fsinto_t));

    fsinfo->FSI_LoadSig = 0x41615252;
    fsinfo->FSI_StrucSig = 0x61417272;
    fsinfo->FSI_Free_Count = 0xFFFFFFFF;
    fsinfo->FSI_Next_Free = 0xFFFFFFFF;         // 根目录不一定从簇2开始
    fsinfo->FSI_TrailSig = 0xAA550000;

    err = xfat_bpool_write_sector(disk, disk_buf, fsinfo_sector);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
    }

    // 同时在备份区中写一个备份
    err = xfat_bpool_write_sector(disk, disk_buf, fsinfo_sector + fmt_info->backup_sector);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
    }

    return FS_ERR_OK;
}

/**
 * 格式化FAT文件系统
 * @param xdisk_part 分区结构
 * @param ctrl 格式化参数
 * @return
 */
xfat_err_t xfat_format (xdisk_part_t * xdisk_part, xfat_fmt_ctrl_t * ctrl) {
    xfat_err_t err;
    xfat_fmt_info_t fmt_info;
    xfat_buf_t * disk_buf;

    // 文件系统支持检查
    if (!xfat_is_fs_supported(ctrl->type)) {
        return FS_ERR_INVALID_FS;
    }

    // 创建dbr头
    err = create_dbr(xdisk_part, ctrl, &fmt_info);
    if (err < 0) {
        return err;
    }

    // 写FAT区
    err = create_fat_table(&fmt_info, xdisk_part, ctrl);
    if (err < 0) {
        return err;
    }

    // 创建首目录区
    err = create_root_dir(&fmt_info, xdisk_part, ctrl);
    if (err < 0) {
        return err;
    }

    // 写fsinfo区，备份区
    err = create_fsinfo(&fmt_info, xdisk_part, ctrl);
    if (err < 0) {
        return err;
    }

    return err;
}

/**
 * 获取指定簇号的第一个扇区编号
 * @param xfat xfat结构
 * @param cluster_no  簇号
 * @return 扇区号
 */
u32_t cluster_fist_sector(xfat_t *xfat, u32_t cluster_no) {
    u32_t data_start_sector = xfat->fat_start_sector + xfat->fat_tbl_sectors * xfat->fat_tbl_nr;
    return data_start_sector + (cluster_no - 2) * xfat->sec_per_cluster;    // 前两个簇号保留
}

int cluster_fat_sector(xfat_t * xfat, u32_t cluster_no) {
    u32_t sector_no = to_sector(xfat_get_disk(xfat), cluster_no * CLUSTER_WIDTH);
    return xfat->fat_start_sector + sector_no;
}

int cluster_base_for_fat_sector(xfat_t * xfat, u32_t sector_no) {
    xdisk_t * disk = xfat_get_disk(xfat);
    u32_t cluster_base = (sector_no - xfat->fat_start_sector) * disk->sector_size / CLUSTER_WIDTH;
    return cluster_base;
}

/**
 * 检查指定簇是否可用，非占用或坏簇
 * @param cluster 待检查的簇
 * @return
 */
int is_cluster_valid(u32_t cluster) {
    cluster &= 0x0FFFFFFF;
    return (cluster < 0x0FFFFFF0) && (cluster >= 0x2);     // 值是否正确
}
    
/**
 * 获取指定簇的下一个簇
 * @param xfat xfat结构
 * @param curr_cluster_no
 * @param next_cluster
 * @return
 */
xfat_err_t get_next_cluster(xfat_t * xfat, u32_t curr_cluster_no, u32_t * next_cluster) {
    if (is_cluster_valid(curr_cluster_no)) {
        cluster32_t * cluster32_buf = (cluster32_t *)xfat->fat_buffer;
        *next_cluster = cluster32_buf[curr_cluster_no].s.next;
    } else {
        *next_cluster = CLUSTER_INVALID;
    }

    return FS_ERR_OK;
}

/**
 * 清除指定簇的所有数据内容，内容填0
 * @param xfat
 * @param cluster
 * @param erase_state 擦除状态的字节值
 * @return
 */
static xfat_err_t erase_cluster(xfat_t * xfat, u32_t cluster, u8_t erase_state) {
    u32_t i;
    u32_t sector = cluster_fist_sector(xfat, cluster);
    xdisk_t * xdisk = xfat_get_disk(xfat);
    xfat_buf_t * disk_buf;

    // 分配临时缓存
    xfat_err_t err = xdisk_alloc_working_buf(xdisk, &disk_buf);
    if (err < 0) {
        return err;
    }

    // todo: 优化，一次可否擦除多个扇区
    memset(disk_buf->buf, erase_state, xdisk->sector_size);
    for (i = 0; i < xfat->sec_per_cluster; i++) {
        xfat_err_t err = xfat_bpool_write_sector(xdisk, disk_buf, sector + i);
        if (err < 0) {
            xdisk_release_buf(xdisk, disk_buf);
            return err;
        }
    }

    xdisk_release_buf(xdisk, disk_buf);
    return FS_ERR_OK;
}

/**
 * 分配空闲簇
 * @param xfat xfat结构
 * @param curr_cluster 当前簇号
 * @param count 要分配的簇号
 * @param start_cluster 分配的第一个可用簇号
 * @param r_allocated_count 有效分配的数量
 * @param erase_cluster 是否同时擦除簇对应的数据区
 * @return
 */
static xfat_err_t allocate_free_cluster(xfat_t * xfat, u32_t curr_cluster, u32_t count,
        u32_t * r_start_cluster, u32_t * r_allocated_count, u8_t en_erase, u8_t erase_data) {
    u32_t i;
    xfat_err_t err;
    xdisk_t * disk = xfat_get_disk(xfat);
    u32_t allocated_count = 0;
    u32_t start_cluster = 0;

    // todo:目前简单起见，从头开始查找, 用于FAT32
    u32_t cluster_count = xfat->fat_tbl_sectors * disk->sector_size / 4;
    u32_t pre_cluster = curr_cluster;
    cluster32_t * cluster32_buf = (cluster32_t *)xfat->fat_buffer;

    for (i = 2; (i < cluster_count) && (allocated_count < count); i++) {
        if (cluster32_buf[i].s.next == 0) {     // 注意是fat32，4字节大小
            if (is_cluster_valid(pre_cluster)) {
                cluster32_buf[pre_cluster].s.next = i;
            }

            pre_cluster = i;

            if (++allocated_count == 1) {
                start_cluster = i;
            }

            if (allocated_count >= count) {
                break;
            }
        }
    }

    if (allocated_count) {
        cluster32_buf[pre_cluster].s.next = CLUSTER_INVALID;

        // 分配簇以后，要注意清空簇中原有内容
        if (en_erase) {
            // 逐个擦除所有的簇
            u32_t cluster = start_cluster;
            for (i = 0; i < allocated_count; i++) {
                err = erase_cluster(xfat, cluster, erase_data);
                if (err < 0) {
                    return err;
                }

                cluster = cluster32_buf[cluster].s.next;
            }
        }

        // FAT表项可能有多个，同时更新
        for (i = 0; i < xfat->fat_tbl_nr; i++) {
            u32_t start_sector = xfat->fat_start_sector + xfat->fat_tbl_sectors * i;
            err = xdisk_write_sector(disk, (u8_t *)xfat->fat_buffer, start_sector, xfat->fat_tbl_sectors);
            if (err < 0) {
                return err;
            }
        }
    }

    if (r_allocated_count) {
        *r_allocated_count = allocated_count;
    }

    if (r_start_cluster) {
        *r_start_cluster = start_cluster;
    }


    return FS_ERR_OK;
}

/**
 * 读取一个簇的内容到指定缓冲区
 * @param xfat xfat结构
 * @param buffer 数据存储的缓冲区
 * @param cluster 读取的起始簇号
 * @param count 读取的簇数量
 * @return
 */
xfat_err_t read_cluster(xfat_t *xfat, u8_t *buffer, u32_t cluster, u32_t count) {
    xfat_err_t err = 0;
    u32_t i = 0;
    u8_t * curr_buffer = buffer;
    u32_t curr_sector = cluster_fist_sector(xfat, cluster);

    for (i = 0; i < count; i++) {
        err = xdisk_read_sector(xfat_get_disk(xfat), curr_buffer, curr_sector, xfat->sec_per_cluster);
        if (err < 0) {
            return err;
        }

        curr_buffer += xfat->sec_per_cluster * xfat->cluster_byte_size;
        curr_sector += xfat->sec_per_cluster;
    }

    return FS_ERR_OK;
}


/**
 * 解除簇的链接关系
 * @param xfat xfat结构
 * @param cluster 将该簇之后的所有链接依次解除, 并将该簇之后标记为解囊
 * @return
 */
static xfat_err_t destroy_cluster_chain(xfat_t *xfat, u32_t cluster) {
    xfat_err_t err = FS_ERR_OK;
    u32_t i, write_back = 0;
    xdisk_t * disk = xfat_get_disk(xfat);
    u32_t curr_cluster = cluster;

    // 先在缓冲区中解除链接关系
    while (is_cluster_valid(curr_cluster)) {
        u32_t next_cluster;
        cluster32_t * cluster32_buf;

        // 先获取一下簇
        err = get_next_cluster(xfat, curr_cluster, &next_cluster);
        if (err < 0) {
            return err;
        }

        // 标记该簇为空闲状态
        cluster32_buf = (cluster32_t *)xfat->fat_buffer;
        cluster32_buf[curr_cluster].s.next = CLUSTER_FREE;

        curr_cluster = next_cluster;
        write_back = 1;
    }

    if (write_back) {
        for (i = 0; i < xfat->fat_tbl_nr; i++) {
            u32_t start_sector = xfat->fat_start_sector + xfat->fat_tbl_sectors * i;
            err = xdisk_write_sector(disk, (u8_t *)xfat->fat_buffer, start_sector, xfat->fat_tbl_sectors);
            if (err < 0) {
                return err;
            }
        }
    }

    // todo: 优化，不必要全部重写
    return err;
}

/**
 * 将指定的name按FAT 8+3命名转换
 * @param dest_name
 * @param my_name
 * @return
 */
static xfat_err_t to_sfn(char * dest_name, const char * my_name, u8_t case_config) {
    int i, name_len;
    char * dest = dest_name;
    const char * ext_dot;
    const char * p;
    int ext_existed;

    memset(dest, ' ', SFN_LEN);

    // 跳过开头的分隔符
    while (is_path_sep(*my_name)) {
        my_name++;
    }

    // 找到第一个斜杠之前的字符串，将ext_dot定位到那里，且记录有效长度
    ext_dot = my_name;
    p = my_name;
    name_len = 0;
    while ((*p != '\0') && !is_path_sep(*p)) {
        if (*p == '.') {
            ext_dot = p;
        }
        p++;
        name_len++;
    }

    // 如果文件名以.结尾，意思就是没有扩展名？
    // todo: 长文件名处理?
    ext_existed = (ext_dot > my_name) && (ext_dot < (my_name + name_len - 1));

    // 遍历名称，逐个复制字符, 算上.分隔符，最长12字节，如果分离符，则只应有
    p = my_name;
    for (i = 0; (i < SFN_LEN) && (*p != '\0') && (*p != '/') && (*p != '\\'); i++) {
        u8_t set_lower = 0;

        if (ext_existed) {
            if (p == ext_dot) {
                dest = dest_name + 8;        // 直接定位到扩展名部分开始写
                p++;
                i--;
                continue;
            } else if (p < ext_dot) {
                set_lower = case_config & DIRITEM_NTRES_BODY_LOWER;
            } else {
                set_lower = case_config & DIRITEM_NTRES_EXT_LOWER;
            }
        } else {
            set_lower = case_config & DIRITEM_NTRES_BODY_LOWER;
        }

        if (set_lower) {
            *dest++ = tolower(*p++);
        } else {
            *dest++ = toupper(*p++);
        }
    }
    return FS_ERR_OK;
}


/**
 *  检查sfn字符串中是否是大写。如果中间有任意小写，都认为是小写
 * @param name
 * @return
 */
static u8_t get_sfn_case_cfg(const char * myname) {
    u8_t case_cfg = 0;

    int i, name_len;
    const char * src_name = myname;
    const char * ext_dot;
    const char * p;
    int ext_existed;

    // 跳过开头的分隔符
    while (is_path_sep(*src_name)) {
        src_name++;
    }

    // 找到第一个斜杠之前的字符串，将ext_dot定位到那里，且记录有效长度
    ext_dot = src_name;
    p = src_name;
    name_len = 0;
    while ((*p != '\0') && !is_path_sep(*p)) {
        if (*p == '.') {
            ext_dot = p;
        }
        p++;
        name_len++;
    }

    // 如果文件名以.结尾，意思就是没有扩展名？
    // todo: 长文件名处理?
    ext_existed = (ext_dot > src_name) && (ext_dot < (src_name + name_len - 1));
    for (p = src_name; p < src_name + name_len; p++) {
        if (ext_existed) {
            if (p < ext_dot) { // 文件名主体部分大小写判断
                case_cfg |= islower(*p) ? DIRITEM_NTRES_BODY_LOWER : 0;
            } else if (p > ext_dot) {
                case_cfg |= islower(*p) ? DIRITEM_NTRES_EXT_LOWER : 0;
            }
        } else {
            case_cfg |= islower(*p) ? DIRITEM_NTRES_BODY_LOWER : 0;
        }
    }

    return case_cfg;
}

/**
 * 检查sfn文件名是否合法
 * @param name
 * @return
 */
static u8_t is_sfn_legal(const char * name) {
    const char * c = name;
    int dot_count = 0;

    // 首字符不能是.，不然没文件名
    if (name[0] == '.') {
        return 0;
    }

    while (*c != '\0') {
        if (*c < 0x20) {
            return 0;
        }

        // .仅允许出现一次
        if ((*c == '.') && (++dot_count >= 2)) {
            return 0;
        }

        switch (*c) {
        case 0x22:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        // case 0x2E: 即字符 . ，已经在switch判断过了
        case 0x2F:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x7C:
            return 0;
        }

        c++;
    }

    return 1;
}

/**
 * 判断两个文件名是否匹配
 * @param name_in_item fatdir中的文件名格式
 * @param my_name 应用可读的文件名格式
 * @return
 */
static u8_t is_filename_match(const char *name_in_item, const char *my_name, u8_t case_config) {
    char temp_name[SFN_LEN];

    // FAT文件名的比较检测等，全部转换成大写比较
    // 根据目录的大小写配置，将其转换成8+3名称，再进行逐字节比较
    // 但实际显示时，会根据diritem->NTRes进行大小写转换
    to_sfn(temp_name, my_name, DIRITEM_NTRES_ALL_UPPER);
    return memcmp(temp_name, name_in_item, SFN_LEN) == 0;
}

/**
 * 跳过开头的分隔符
 * @param path 目标路径
 * @return
 */
static const char * skip_first_path_sep (const char * path) {
    const char * c = path;

    if (c == (const char *)0) {
        return (const char *)0;
    }

    // 跳过开头的分隔符
    while (is_path_sep(*c)) {
        c++;
    }
    return c;
}

/**
 * 获取子路径
 * @param dir_path 上一级路径
 * @return
 */
const char * get_child_path(const char *dir_path) {
    const char * c = skip_first_path_sep(dir_path);

    // 跳过父目录
    while ((*c != '\0') && !is_path_sep(*c)) {
        c++;
    }

    return (*c == '\0') ? (const char *)0 : c + 1;
}

/**
 * 解析diritem，获取文件类型
 * @param diritem 需解析的diritem
 * @return
 */
static xfile_type_t get_file_type(const diritem_t *diritem) {
    xfile_type_t type;

    if (diritem->DIR_Attr & DIRITEM_ATTR_VOLUME_ID) {
        type = FAT_VOL;
    } else if (diritem->DIR_Attr & DIRITEM_ATTR_DIRECTORY) {
        type = FAT_DIR;
    } else {
        type = FAT_FILE;
    }

    return type;
}

/**
 * 复制相应的时间信息到dest中
 * @param dest 指定存储的时间信息结构
 * @param date fat格式的日期
 * @param time fat格式的时间
 * @param mil_sec fat格式的10毫秒
 */
static void copy_date_time(xfile_time_t *dest, const diritem_date_t *date,
                           const diritem_time_t *time, const u8_t mil_sec) {
    if (date) {
        dest->year = (u16_t)(date->year_from_1980 + 1980);
        dest->month = (u8_t)date->month;
        dest->day = (u8_t)date->day;
    } else {
        dest->year = 0;
        dest->month = 0;
        dest->day = 0;
    }

    if (time) {
        dest->hour = (u8_t)time->hour;
        dest->minute = (u8_t)time->minute;
        dest->second = (u8_t)(time->second_2 * 2 + mil_sec / 100);
    } else {
        dest->hour = 0;
        dest->minute = 0;
        dest->second = 0;
    }
}

/**
 * 从fat_dir格式的文件名中拷贝成用户可读的文件名到dest_name
 * @param dest_name 转换后的文件名存储缓冲区
 * @param raw_name fat_dir格式的文件名
 */
static void sfn_to_myname(char *dest_name, const diritem_t * diritem) {
    int i;
    char * dest = dest_name, * raw_name = (char *)diritem->DIR_Name;
    u8_t ext_exist = raw_name[8] != 0x20;
    u8_t scan_len = ext_exist ? SFN_LEN + 1 : SFN_LEN;

    memset(dest_name, 0, X_FILEINFO_NAME_SIZE);

    // 要考虑大小写问题，根据NTRes配置转换成相应的大小写
    for (i = 0; i < scan_len; i++) {
        if (*raw_name == ' ') {
            raw_name++;
        } else if ((i == 8) && ext_exist) {
           *dest++ = '.';
        } else {
            u8_t lower = 0;

            if (((i < 8) && (diritem->DIR_NTRes & DIRITEM_NTRES_BODY_LOWER))
                || ((i > 8) && (diritem->DIR_NTRes & DIRITEM_NTRES_EXT_LOWER))) {
                lower = 1;
            }

            *dest++ = lower ? tolower(*raw_name++) : toupper(*raw_name++);
        }
    }
    *dest = '\0';
}

/**
 * 设置diritem的cluster
 * @param item 目录diritem
 * @param cluster 簇号
 */
static void set_diritem_cluster (diritem_t * item, u32_t cluster) {
    item->DIR_FstClusHI = (u16_t )(cluster >> 16);
    item->DIR_FstClusL0 = (u16_t )(cluster & 0xFFFFF);
}

/**
 * 获取diritem的文件起始簇号
 * @param item
 * @return
 */
static u32_t get_diritem_cluster (diritem_t * item) {
    return (item->DIR_FstClusHI << 16) | item->DIR_FstClusL0;
}

/**
 * 将dir_item中相应的文件信息转换存至fs_fileinfo_t中
 * @param info 信息存储的位置
 * @param dir_item fat的diritem
 */
static void copy_file_info(xfileinfo_t *info, const diritem_t * dir_item) {
    sfn_to_myname(info->file_name, dir_item);
    info->size = dir_item->DIR_FileSize;
    info->attr = dir_item->DIR_Attr;
    info->type = get_file_type(dir_item);

    copy_date_time(&info->create_time, &dir_item->DIR_CrtDate, &dir_item->DIR_CrtTime, dir_item->DIR_CrtTimeTeenth);
    copy_date_time(&info->last_acctime, &dir_item->DIR_LastAccDate, (diritem_time_t *) 0, 0) ;
    copy_date_time(&info->modify_time, &dir_item->DIR_WrtDate, &dir_item->DIR_WrtTime, 0);
}

/**
 * 检查文件名和类型是否匹配
 * @param dir_item
 * @param locate_type
 * @return
 */
static u8_t is_locate_type_match (diritem_t * dir_item, u8_t locate_type) {
    u8_t match = 1;

    if ((dir_item->DIR_Attr & DIRITEM_ATTR_SYSTEM) && !(locate_type & XFILE_LOCALE_SYSTEM)) {
        match = 0;  // 不显示系统文件
    } else if ((dir_item->DIR_Attr & DIRITEM_ATTR_HIDDEN) && !(locate_type & XFILE_LOCATE_HIDDEN)) {
        match = 0;  // 不显示隐藏文件
    } else if ((dir_item->DIR_Attr & DIRITEM_ATTR_VOLUME_ID) && !(locate_type & XFILE_LOCATE_VOL)) {
        match = 0;  // 不显示卷标
    } else if ((memcmp(DOT_FILE, dir_item->DIR_Name, SFN_LEN) == 0)
                || (memcmp(DOT_DOT_FILE, dir_item->DIR_Name, SFN_LEN) == 0)) {
        if (!(locate_type & XFILE_LOCATE_DOT)) {
            match = 0;// 不显示dot文件
        }
    } else if (!(locate_type & XFILE_LOCATE_NORMAL)) {
        match = 0;
    }
    return match;
}

/**
 * 查找指定dir_item，并返回相应的结构
 * @param xfat xfat结构
 * @param locate_type 定位的item类型
 * @param dir_cluster dir_item所在的目录数据簇号
 * @param cluster_offset 簇中的偏移
 * @param move_bytes 查找到相应的item项后，相对于最开始传入的偏移值，移动了多少个字节才定位到该item
 * @param name 文件或目录的名称
 * @param r_diritem 查找到的diritem项
 * @return
 */
static xfat_err_t next_file_dir_item(xfat_t *xfat, u8_t locate_type, u32_t *dir_cluster, u32_t *cluster_offset,
                                    const char *name, u32_t *move_bytes, diritem_t **r_diritem) {
    u32_t curr_cluster = *dir_cluster;
    xdisk_t * xdisk = xfat_get_disk(xfat);
    u32_t initial_sector = to_sector(xdisk, *cluster_offset);
    u32_t initial_offset = to_sector_offset(xdisk, *cluster_offset);
    u32_t r_move_bytes = 0;

    // cluster
    do {
        u32_t i;
        xfat_err_t err;
        u32_t start_sector = cluster_fist_sector(xfat, curr_cluster);

        for (i = initial_sector; i < xfat->sec_per_cluster; i++) {
            u32_t j;
            xfat_buf_t * disk_buf;

            err = xfat_bpool_read_sector(xdisk, &disk_buf, start_sector + i);
            if (err < 0) {
                return err;
            }

            for (j = initial_offset / sizeof(diritem_t); j < xdisk->sector_size / sizeof(diritem_t); j++) {
                diritem_t *dir_item = ((diritem_t *)disk_buf->buf) + j;

                if (dir_item->DIR_Name[0] == DIRITEM_NAME_END) {
                    xdisk_release_buf(xdisk, disk_buf);
                    return FS_ERR_EOF;
                } else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
                    r_move_bytes += sizeof(diritem_t);
                    continue;
                } else if (!is_locate_type_match(dir_item, locate_type)) {
                    r_move_bytes += sizeof(diritem_t);
                    continue;
                }

                if ((name == (const char *) 0)
                    || (*name == 0)
                    || is_filename_match((const char *) dir_item->DIR_Name, name, dir_item->DIR_NTRes)) {
                    u32_t total_offset = i * xdisk->sector_size + j * sizeof(diritem_t);
                    *dir_cluster = curr_cluster;
                    *move_bytes = r_move_bytes + sizeof(diritem_t);
                    *cluster_offset = total_offset;
                    if (r_diritem) {
                        *r_diritem = dir_item;
                    }

                    xdisk_release_buf(xdisk, disk_buf);
                    return FS_ERR_OK;
                }

                r_move_bytes += sizeof(diritem_t);

            }

            xdisk_release_buf(xdisk, disk_buf);
        }

        err = get_next_cluster(xfat, curr_cluster, &curr_cluster);
        if (err < 0) {
            return err;
        }

        initial_sector = 0;
        initial_offset = 0;
    }while (is_cluster_valid(curr_cluster));

    return FS_ERR_EOF;
}

/**
 * 从磁盘加载空闲cluster给fat
 * 最后实际查找到的数量可能比要求的要少
 * @param xfat
 * @param request_count 申请加载的数量，如果为0，则尽可能加载越多越好
 * @return
 */
static xfat_err_t load_clusters_for_file(xfile_t * file, u32_t request_count) {
    xfat_err_t err;
    xdisk_t * disk = file_get_disk(file);
    xfat_t * xfat = file->xfat;
    u32_t loaded_count = 0;
    u32_t curr_cluster, next_cluster;
    u32_t begin_cluster, end_cluster;
    u32_t pre_sector = 0;
    xfat_buf_t * disk_buf = (xfat_buf_t *)0;

    // 获取起始簇，如果是空簇链，直接结束
    curr_cluster = is_cluster_valid(file->curr_cluster) ? file->curr_cluster : file->start_cluster;
    if (!is_cluster_valid(curr_cluster)) {
        return FS_ERR_OK;
    }
    loaded_count++;

    begin_cluster = curr_cluster;
    end_cluster = curr_cluster;
    while (xcluster_chain_free_count(&file->cluster_chain)) {
        u32_t curr_sector;
        u32_t cluster_rel;

        curr_sector = cluster_fat_sector(xfat, curr_cluster);

        // 当超过读取数量，但如果还可以在同一扇区中搜索，那就继续加载呗
        // 内存访问速度很快，这样就不必劳烦下一次再读扇区加载了
        if (loaded_count >= request_count) {
            if ((pre_sector >= xfat->fat_start_sector) && (curr_sector != pre_sector)) {
                if (is_cluster_valid(begin_cluster)) {
                    xcluster_item_t item;
                    xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                    xcluster_chain_add(&file->cluster_chain, &item, 0);
                }
                break;
            }
        }

        // 如果下一簇和当前簇在同一扇区，则不需要再读一次扇区
        if (curr_sector != pre_sector) {
            xdisk_release_buf(disk, disk_buf);

            err = xfat_bpool_read_sector(disk, &disk_buf, curr_sector);
            if (err < 0) {
                return err;
            }
            pre_sector = curr_sector;
        }

        // 获取下一簇
        cluster_rel = curr_cluster - cluster_base_for_fat_sector(xfat, curr_sector);
        next_cluster = ((cluster32_t *)disk_buf->buf)[cluster_rel].s.next;

        if (is_cluster_valid(next_cluster)) {
            // 文件未结束，有有效后续簇，添加到簇区间中
            ++loaded_count;

            if (begin_cluster == CLUSTER_INVALID) {
                // 新开始的区间
                begin_cluster = next_cluster;
                end_cluster = next_cluster;
            }  else if ((curr_cluster + 1) == next_cluster) {
                // 与当前区间合并
                end_cluster++;
            } else {
                // 不相邻簇， 使用新区间
                xcluster_item_t item;
                xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                xcluster_chain_add(&file->cluster_chain, &item, 0);

                begin_cluster = next_cluster;
                end_cluster = next_cluster;
            }
        } else {
            // 文件结束
            if (is_cluster_valid(begin_cluster)) {
                xcluster_item_t item;
                xcluster_item_init(&item, begin_cluster, end_cluster, XCLUSTER_FLAG_CLEAN);
                xcluster_chain_add(&file->cluster_chain, &item, 0);
            }

            xdisk_release_buf(disk, disk_buf);
            return FS_ERR_OK;
        }

        curr_cluster = next_cluster;
    }

    xdisk_release_buf(disk, disk_buf);
    return FS_ERR_OK;
}

/**
 * 打开指定dir_cluster开始的簇链中包含的子文件。
 * 如果path为空，则以dir_cluster创建一个打开的目录对像
 * @param xfat xfat结构
 * @param dir_cluster 查找的顶层目录的起始簇链
 * @param file 打开的文件file结构
 * @param path 以dir_cluster所对应的目录为起点的完整路径
 * @return
 */
static xfat_err_t open_sub_file (xfat_t * xfat, u32_t dir_cluster, xfile_t * file, const char * path) {
    u32_t parent_cluster = dir_cluster;
    u32_t parent_cluster_offset = 0;
    xfat_err_t err;
    u32_t preload_clustes;

    path = skip_first_path_sep(path);

    // 如果传入路径不为空，则查看子目录
    // 否则，直接认为dir_cluster指向的是一个目录，用于打开根目录
    if ((path != 0) && (*path != '\0')) {
        diritem_t * dir_item = (diritem_t *)0;
        u32_t file_start_cluster = 0;
        const char * curr_path = path;

       // 找到path对应的起始簇
        while (curr_path != (const char *)0) {
            u32_t moved_bytes = 0;
            dir_item = (diritem_t *)0;

            // 在父目录下查找指定路径对应的文件
            err = next_file_dir_item(xfat, XFILE_LOCATE_ALL,
                    &parent_cluster, &parent_cluster_offset,curr_path, &moved_bytes, &dir_item);
            if (err < 0) {
                return err;
            }

            if (dir_item == (diritem_t *)0) {
                return FS_ERR_NONE;
            }

            curr_path = get_child_path(curr_path);
            if (curr_path != (const char *)0) {
                parent_cluster = get_diritem_cluster(dir_item);
                parent_cluster_offset = 0;
            } else {
                file_start_cluster = get_diritem_cluster(dir_item);;
            }
        }

        file->size = dir_item->DIR_FileSize;
        file->type = get_file_type(dir_item);
        file->attr = (dir_item->DIR_Attr & DIRITEM_ATTR_READ_ONLY) ? XFILE_ATTR_READONLY : 0;
        file->start_cluster = file_start_cluster;
        file->curr_cluster = file_start_cluster;
        file->dir_cluster = parent_cluster;
        file->dir_cluster_offset = parent_cluster_offset;
    } else {
        file->size = 0;
        file->type = FAT_DIR;
        file->attr = 0;
        file->start_cluster = parent_cluster;
        file->curr_cluster = parent_cluster;
        file->dir_cluster = CLUSTER_INVALID;
        file->dir_cluster_offset = 0;
    }

    file->xfat = xfat;
    file->pos = 0;
    file->err = FS_ERR_OK;


    if (file->type == FAT_DIR) {
        preload_clustes = XDIR_CLUSTER_PRELOAD_SIZE;
    } else {
        if (XFILE_CLUSTER_PRELOAD_SIZE <= 0) {            
            preload_clustes = (file->size + xfat->cluster_byte_size - 1) / xfat->cluster_byte_size;
        } else {
            preload_clustes = XFILE_CLUSTER_PRELOAD_SIZE;
        }
    }

    // 加载文件或目录的簇链
    xcluster_chain_init(&file->cluster_chain, file->cluster_items, XFILE_CLUSTER_CHAIN_SIZE);
    err = load_clusters_for_file(file, preload_clustes);
    if (err < 0) {
        return err;
    }

    return FS_ERR_OK;
}

/**
 * 打开指定的文件或目录
 * @param xfat xfat结构
 * @param file 打开的文件或目录
 * @param path 文件或目录所在的完整路径
 * @return
 */
xfat_err_t xfile_open(xfile_t * file, const char * path) {
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    return open_sub_file(xfat, xfat->root_cluster, file, path);
}

/**
 * 在打开的目录下，打开相应的子文件或目录
 * @param file  已经打开的目录
 * @param sub_file 打开的子文件或目录
 * @param sub_path 以已打开的目录为起点，子文件或目录的完整路径
 * @return
 */
xfat_err_t xfile_open_sub(xfile_t * file, xfile_t * sub_file, const char * sub_path) {
    return open_sub_file(file->xfat, file->start_cluster, sub_file, sub_path);
}

/**
 * 返回指定目录下的第一个文件信息
 * @param file 已经打开的文件
 * @param info 第一个文件的文件信息
 * @return
 */
xfat_err_t xdir_first_file (xfile_t * file, xfileinfo_t * info) {
    diritem_t * diritem = (diritem_t *)0;
    xfat_err_t err;
    u32_t moved_bytes = 0;
    u32_t cluster_offset;

    // 仅能用于目录下搜索
    if (file->type != FAT_DIR) {
        return FS_ERR_PARAM;
    }

    // 重新调整搜索位置
    file->curr_cluster = file->start_cluster;
    file->pos = 0;

    cluster_offset = to_cluster_offset(file->xfat, file->pos);
    err = next_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL,
            &file->curr_cluster, &cluster_offset, "", &moved_bytes, &diritem);
    if (err < 0) {
        return err;
    }

    if (diritem == (diritem_t *)0) {
        return FS_ERR_EOF;
    }

    file->pos += moved_bytes;

    // 找到后，拷贝文件信息
    copy_file_info(info, diritem);
    return err;
}

/**
 * 返回指定目录接下来的文件（用于文件遍历)
 * @param file 已经打开的目录
 * @param info 获得的文件信息
 * @return
 */
xfat_err_t xdir_next_file (xfile_t * file, xfileinfo_t * info) {
    xfat_err_t err;
    diritem_t * dir_item = (diritem_t *)0;
    u32_t moved_bytes = 0;
    u32_t cluster_offset;

    // 仅用于目录
    if (file->type != FAT_DIR) {
        return FS_ERR_PARAM;
    }

    // 搜索文件或目录
    cluster_offset = to_cluster_offset(file->xfat, file->pos);
    err = next_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL,
            &file->curr_cluster, &cluster_offset, "", &moved_bytes, &dir_item);
    if (err != FS_ERR_OK) {
        return err;
    }

    if (dir_item == (diritem_t *)0) {
        return FS_ERR_EOF;
    }

    file->pos += moved_bytes;

    // 移动位置后，可能超过当前簇，更新当前簇位置
    if (cluster_offset + moved_bytes >= file->xfat->cluster_byte_size) {
        err = get_next_cluster(file->xfat, file->curr_cluster, &file->curr_cluster);
        if (err < 0) {
            return err;
        }
    }

    copy_file_info(info, dir_item);
    return err;
}

/**
 * 获取文件读写的错误码
 * @param file
 * @return
 */
xfat_err_t xfile_error(xfile_t * file) {
    return file->err;
}

/**
 * 清除文件读写错误状态码
 * @param file
 */
void xfile_clear_err(xfile_t * file) {
    file->err = FS_ERR_OK;
}

/**
 * 缺省初始化driitem
 * @param dir_item 待初始化的diritem
 * @param is_dir 该项是否对应目录项
 * @param name 项的名称
 * @param cluster 数据簇的起始簇号
 * @return
 */
static xfat_err_t diritem_init_default(diritem_t * dir_item, xdisk_t * disk, u8_t is_dir, const char * name, u32_t cluster) {
    xfile_time_t timeinfo;

    xfat_err_t err = xdisk_curr_time(disk, &timeinfo);
    if (err < 0) {
        return err;
    }

    to_sfn((char *)dir_item->DIR_Name, name, 0);
    set_diritem_cluster(dir_item, cluster);
    dir_item->DIR_FileSize = 0;
    dir_item->DIR_Attr = (u8_t)(is_dir ? DIRITEM_ATTR_DIRECTORY : 0);
    dir_item->DIR_NTRes = get_sfn_case_cfg(name);

    dir_item->DIR_CrtTime.hour = timeinfo.hour;
    dir_item->DIR_CrtTime.minute = timeinfo.minute;
    dir_item->DIR_CrtTime.second_2 = (u16_t)(timeinfo.second / 2);
    dir_item->DIR_CrtTimeTeenth = (u8_t)((timeinfo.second & 1) * 1000 + timeinfo.mil_second);

    dir_item->DIR_CrtDate.year_from_1980 = (u16_t)(timeinfo.year - 1980);
    dir_item->DIR_CrtDate.month = timeinfo.month;
    dir_item->DIR_CrtDate.day = timeinfo.day;

    dir_item->DIR_WrtTime = dir_item->DIR_CrtTime;
    dir_item->DIR_WrtDate = dir_item->DIR_CrtDate;
    dir_item->DIR_LastAccDate = dir_item->DIR_CrtDate;

    return FS_ERR_OK;
}

/**
 * 在指定目录下创建子文件或者目录
 * @param xfat xfat结构
 * @param is_dir 要创建的是文件还是目录
 * @param parent_dir_cluster 父目录起始数据簇号
 * @param file_cluster 预先给定的文件或目录的起始数据簇号。如果文件或目录已经存在，则返回相应的簇号
 * @param child_name 创建的目录或文件名称
 * @return
 */
static xfat_err_t create_sub_file (xfat_t * xfat, u8_t is_dir, u32_t parent_cluster, 
                    u32_t * file_cluster, const char * child_name) {
    xfat_err_t err;
    xdisk_t * disk = xfat_get_disk(xfat);
    diritem_t * target_item = (diritem_t *)0;
    u32_t pre_cluster = CLUSTER_INVALID;
    u32_t curr_sector;
    xfat_buf_t * disk_buf = 0;

    // 遍历整个父目录项的所有数据簇，找到同名的，若找不同则找到空闲的目录项
    while (is_cluster_valid(parent_cluster)) {
        u32_t cluster_offset = 0;
        u32_t sector_offset = 0;

        // 逐个遍历当前簇中的所有dir_item
        for (cluster_offset = 0; cluster_offset < xfat->cluster_byte_size; cluster_offset += sizeof(diritem_t)) {
            diritem_t *dir_item;
            curr_sector = to_phy_sector(xfat, parent_cluster, cluster_offset);    // 获取当前依稀的物理扇区号

            // 当簇偏移遇到扇区边界时，读取扇区
            if (cluster_offset % disk->sector_size == 0) {
                xdisk_release_buf(disk, disk_buf);

                err = xfat_bpool_read_sector(disk, &disk_buf, curr_sector);
                if (err < 0) {
                    return err;
                }

                // 取当前遍历的扇区在整个簇中的偏移
                sector_offset = to_sector_addr(disk, cluster_offset);

                // 重新读取后，target_item无效，需要重新扫描
                target_item = (diritem_t *)0;
            }

            // 检查空项，结束标记等
            dir_item = (diritem_t *)(disk_buf->buf + (cluster_offset - sector_offset));
            if (dir_item->DIR_Name[0] == DIRITEM_NAME_END) {        // 有效结束标记
                target_item = dir_item;
                goto search_end;
            } else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
                // 空闲项, 还要继续检查，看是否有同名项
                target_item = dir_item;
            } else if (is_filename_match((const char *)dir_item->DIR_Name, child_name, 0)) {
                // 仅名称相同，还要检查是否是同名的文件或目录
                int item_is_dir = dir_item->DIR_Attr & DIRITEM_ATTR_DIRECTORY;
                if ((is_dir && item_is_dir) || (!is_dir && !item_is_dir)) { // 同类型且同名
                    *file_cluster = get_diritem_cluster(dir_item);  // 返回

                    xdisk_release_buf(disk, disk_buf);
                    return FS_ERR_EXISTED;
                } else {    // 不同类型，即目录-文件同名，直接报错
                    xdisk_release_buf(disk, disk_buf);
                    return FS_ERR_NAME_USED;
                }
            }
        }

        // 当前簇未找到，继续下一簇
        pre_cluster = parent_cluster;
        err = get_next_cluster(xfat, pre_cluster, &parent_cluster);
        if (err < 0) {
            return err;
        }
    }

search_end:
    // 如果在目录的数据簇中未找到空闲的项，需要申请新簇，然后使用其中的项
    if (target_item == (diritem_t *)0) {
        xdisk_release_buf(disk, disk_buf);

        // 分配新簇，同时清0，建立链接关系
        u32_t cluster_count = 0;
        xfat_err_t err = allocate_free_cluster(xfat, pre_cluster, 1, &parent_cluster, &cluster_count, 1, 0);
        if (err < 0) {
            return err;
        }

        // 分配失败，没有可用的分配簇
        if (cluster_count < 1) {
            return FS_ERR_DISK_FULL;
        }

        // 读取新建簇中的第一个扇区，获取target_item
        curr_sector = cluster_fist_sector(xfat, parent_cluster);
        err = xfat_bpool_read_sector(disk, &disk_buf, curr_sector);
        if (err < 0) {
            return err;
        }
        target_item = (diritem_t *)disk_buf->buf;     // 获取新簇项
    }

    // 获取目录项之后，根据文件或目录，创建item
    err = diritem_init_default(target_item, disk, is_dir, child_name, *file_cluster);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
    }

    // 如果是目录且不为dot file， 为其分配目录簇的空间
    if (is_dir && strcmp(".", child_name) && strcmp("..", child_name)) {
        u32_t cluster;

        // 注意，在此不擦除，因为擦除会使用缓存，这会破坏当前diritem的缓存区
        err = allocate_free_cluster(xfat, CLUSTER_INVALID, 1, &cluster, 0, 0, 0);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }
        set_diritem_cluster(target_item, cluster);

        // 回写扇区空项
        err = xfat_bpool_write_sector(disk, disk_buf, curr_sector);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }
        xdisk_release_buf(disk, disk_buf);

        // 创建目录项之后，要清空为目录项分配的空间
        err = erase_cluster(xfat, cluster, 0x00);
        if (err < 0) {
            return err;
        }
        *file_cluster = cluster;
        return FS_ERR_OK;
    } else {
        // 回写扇区空项
        err = xfat_bpool_write_sector(disk, disk_buf, curr_sector);
        if (err < 0) {
            xdisk_release_buf(disk, disk_buf);
            return err;
        }

        xdisk_release_buf(disk, disk_buf);
        *file_cluster = get_diritem_cluster(target_item);
    }

    return err;
}

/**
 * 创建一个空目录
 * @param xfat xfat结构
 * @param parent_cluster 空目录所在的父目录的起始数据簇
 * @param fail_on_exist 目录已经存在时，是否认为是失败
 * @param name 空目录的名称
 * @param dir_cluster 创建好之后，该新目录的数据起始簇号
 * @return
 */
static xfat_err_t create_empty_dir (xfat_t * xfat, u8_t fail_on_exist, u32_t parent_cluster,
                                const char * name, u32_t * new_cluster) {
    u32_t dot_cluster;
    u32_t dot_dot_cluster;
    xfat_err_t err;

    // 创建三个文件：指定目录，面的.和..子目录
    err = create_sub_file(xfat, 1, parent_cluster, new_cluster, name);
    if ((err == FS_ERR_EXISTED) && !fail_on_exist) {
        return FS_ERR_OK;   // 允许文件已存在，则直接退出
    } else if (err < 0) {
        return err;
    }

    // 在新创建的目录下创建文件 . ，其簇号为当前目录的簇号
    dot_cluster = *new_cluster;
    err = create_sub_file(xfat, 1, *new_cluster, &dot_cluster, ".");
    if (err < 0) {
        return err;
    }

    // 分配文件 .. ，其簇号为父目录的簇号
    dot_dot_cluster = parent_cluster;
    err = create_sub_file(xfat, 1, *new_cluster, &dot_dot_cluster, "..");
    if (err < 0) {
        return err;
    }

    return FS_ERR_OK;
}

/**
 * 按指定路径，创建目录, 如果目录已经存在，不会报错
 * @param xfat
 * @param dir_path
 * @return
 */
xfat_err_t xfile_mkdir (const char * path) {
    u32_t parent_cluster;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    parent_cluster = xfat->root_cluster;

    // 遍历解析文件路径, 逐级创建各个目录项
    while ((path != 0) && (*path != '\0')) {
        u32_t new_dir_cluster = 0;
        const char * next_path = get_child_path(path);
        u8_t fail_on_exist = is_path_end(next_path);        // 中间目录创建，不会失败

        xfat_err_t err = create_empty_dir(xfat, fail_on_exist, parent_cluster, path, &new_dir_cluster);
        if (err < 0) {
            return err;
        }

        path = get_child_path(path);
        parent_cluster = new_dir_cluster;
    }
    return FS_ERR_OK;
}

/**
 * 按指定路径，依次创建各级目录，最后创建指定文件
 * @param xfat xfat结构
 * @param file_path 文件的完整路径
 * @return
 */
xfat_err_t xfile_mkfile (const char * path) {
    u32_t parent_cluster;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    // 默认从根目录创建
    parent_cluster = xfat->root_cluster;

    // 逐级创建目录和文件
    while (!is_path_end(path)) {
        xfat_err_t err;
        u32_t file_cluster = FILE_DEFAULT_CLUSTER;
        const char * next_path = get_child_path(path);

        // 没有后续路径，则当前创建文件
        if (is_path_end(next_path)) {
            xfat_err_t err = create_sub_file(xfat, 0, parent_cluster, &file_cluster, path);
            return err;
        } else {
            // 在此创建目录, 如果目录已经存在，则继续
            err = create_empty_dir(xfat, 0, parent_cluster, path, &file_cluster);
            if (err < 0) {
                return err;
            }
            parent_cluster = file_cluster;
        }

        path = next_path;
    }
    return FS_ERR_OK;
}

/**
 * 从指定的文件中读取相应个数的元素数据
 * @param buffer 数据存储的缓冲区
 * @param elem_size 每次读取的元素字节大小
 * @param count 读取多少个elem_size
 * @param file 要读取的文件
 * @return
 */
xfile_size_t xfile_read(void * buffer, xfile_size_t elem_size, xfile_size_t count, xfile_t * file) {
    u32_t cluster_sector,  sector_offset;
    xdisk_t * disk = file_get_disk(file);
    u32_t r_count_readed = 0;
    xfile_size_t bytes_to_read = count * elem_size;
    u8_t * read_buffer = (u8_t *)buffer;

    // 只允许直接读普通文件
    if (file->type != FAT_FILE) {
        file->err = FS_ERR_FSTYPE;
        return 0;
    }

    // 已经到达文件尾末，不读
    if (file->pos >= file->size) {
        file->err = FS_ERR_EOF;
        return 0;
    }

    // 无需读取
    if (bytes_to_read == 0) {
        file->err = FS_ERR_OK;
        return 0;
    }

    // 调整读取量，不要超过文件总量
    if (file->pos + bytes_to_read > file->size) {
        bytes_to_read = file->size - file->pos;
    }

    cluster_sector = to_sector(disk, to_cluster_offset(file->xfat, file->pos));  // 簇中的扇区偏移
    sector_offset = to_sector_offset(disk, file->pos);  // 扇区偏移位置

    while ((bytes_to_read > 0) && is_cluster_valid(file->curr_cluster)) {
        xfat_err_t err;
        u32_t curr_read_bytes = 0;
        u32_t sector_count = 0;
        u32_t start_sector = cluster_fist_sector(file->xfat, file->curr_cluster) + cluster_sector;

        // 起始非扇区边界对齐, 只读取当前扇区
        // 或者起始为0，但读取量不超过当前扇区，也只读取当前扇区
        // 无论哪种情况，都需要暂存到缓冲区中，然后拷贝到用户缓冲区
        if ((sector_offset != 0) || (!sector_offset && (bytes_to_read < disk->sector_size))) {
            xfat_buf_t * disk_buf;

            sector_count = 1;
            curr_read_bytes = bytes_to_read;

            // 起始偏移非0，如果跨扇区，只读取当前扇区
            if (sector_offset != 0) {
                if (sector_offset + bytes_to_read > disk->sector_size) {
                    curr_read_bytes = disk->sector_size - sector_offset;
                }
            }

            // 读取整扇区，然后从中间拷贝部分数据到应用缓冲区中
            // todo: 连续多次小批量读时，可能会重新加载同一扇区
            err = xfat_bpool_read_sector(disk, &disk_buf, start_sector);
            if (err < 0) {
                xdisk_release_buf(disk, disk_buf);
                file->err = err;
                return 0;
            }

            memcpy(read_buffer, disk_buf->buf + sector_offset, curr_read_bytes);
            xdisk_release_buf(disk, disk_buf);

            read_buffer += curr_read_bytes;
            bytes_to_read -= curr_read_bytes;
        } else {
            // 起始为0，且读取量超过1个扇区，连续读取多扇区
            sector_count = to_sector(disk, bytes_to_read);

            // 如果超过一簇，则只读取当前簇
            // todo: 这里可以再优化一下，如果簇连续的话，实际是可以连读多簇的
            if ((cluster_sector + sector_count) > file->xfat->sec_per_cluster) {
                sector_count = file->xfat->sec_per_cluster - cluster_sector;
            }

            // 暂不用缓存读，直接写用户内存，速度更快
            err = xdisk_read_sector(disk, read_buffer, start_sector, sector_count);
            if (err != FS_ERR_OK) {
                file->err = err;
                return 0;
            }

            curr_read_bytes = sector_count * disk->sector_size;
            read_buffer += curr_read_bytes;
            bytes_to_read -= curr_read_bytes;
        }
        r_count_readed += curr_read_bytes;
        file->pos += curr_read_bytes;

        // 校正下次读取位置
        sector_offset += curr_read_bytes;
        if (sector_offset >= disk->sector_size) {
            sector_offset = 0;
            cluster_sector += sector_count;

            if (cluster_sector >= file->xfat->sec_per_cluster) {
                u32_t curr_cluster = file->curr_cluster;

                cluster_sector = 0;
                err = get_next_cluster(file->xfat, curr_cluster, &curr_cluster);
                if (err != FS_ERR_OK) {
                    file->err = err;
                    return 0;
                }

                // 没有尾簇了，到头
                if (!is_cluster_valid(curr_cluster)) {
                    file->err = FS_ERR_OK;
                    return r_count_readed / elem_size;;
                }
                file->curr_cluster = curr_cluster;
            }
        }
    }

    file->err = file->size == file->pos;
    return r_count_readed / elem_size;
}

/**
 * 更新已经打开的文件大小
 * @param file 已经打开的文件
 * @param size 文件大小
 * @return
 */
static xfat_err_t update_file_size(xfile_t * file, xfile_size_t size) {
    xfat_err_t err;
    diritem_t * dir_item;
    xdisk_t * disk = file_get_disk(file);
    u32_t sector = to_phy_sector(file->xfat, file->dir_cluster, file->dir_cluster_offset);
    u32_t offset = to_sector_offset(disk, file->dir_cluster_offset);
    xfat_buf_t * disk_buf;

    err = xfat_bpool_read_sector(disk, &disk_buf, sector);
    if (err < 0) {
        file->err = err;
        return err;
    }

    dir_item = (diritem_t *)(disk_buf->buf + offset);
    dir_item->DIR_FileSize = size;

    // 注意更新簇号，因初始文件创建时分配的簇可能并不有效，需要重新设置
    set_diritem_cluster(dir_item, file->start_cluster);
 
    err = xfat_bpool_write_sector(disk, disk_buf, sector);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);

        file->err = err;
        return err;
    }

    file->size = size;
    xdisk_release_buf(disk, disk_buf);
    return FS_ERR_OK;
}

/**
 * 判断文件当前指针是否是结尾簇的末端
 */
static int is_fpos_cluster_end(xfile_t * file) {
    xfile_size_t cluster_offset = to_cluster_offset(file->xfat, file->pos);
    return is_cluster_valid(file->curr_cluster) && (cluster_offset == 0) && (file->pos == file->size);
}

/**
 * 扩充文件大小，新增的文件数据部分，其内容由mode来控制
 * @param file 待扩充的文件
 * @param size 新的文件大小
 * @param mode 扩充模式
 * @return
 */
static xfat_err_t expand_file(xfile_t * file, xfile_size_t size) {
    xfat_err_t err;
    u32_t curr_cluster_nr;
    u32_t request_cluster_nr;
    u32_t curr_cluster_no;
    xfat_t * xfat = file->xfat;

    curr_cluster_nr = (file->size + xfat->cluster_byte_size - 1) / xfat->cluster_byte_size;
    request_cluster_nr = (size + xfat->cluster_byte_size - 1) / xfat->cluster_byte_size;
    curr_cluster_no = file->pos / xfat->cluster_byte_size;

    // 当指向尾簇末端时，实际比计算的少一个簇
    if (is_fpos_cluster_end(file)) {
        curr_cluster_nr--;
        curr_cluster_no--;
    }

    // 当扩充容量需要跨簇时，在簇链之后增加新项
    if (curr_cluster_nr < request_cluster_nr) {
        u32_t cluster_cnt = request_cluster_nr - curr_cluster_nr;
        u32_t start_free_cluster = 0;
        u32_t curr_culster = file->curr_cluster;
        u32_t allocated_count = 0;

        // 先定位至文件的最后一簇, 仅需要定位文件大小不为0的簇
        if (file->size > 0) {
            while (curr_cluster_no++ < curr_cluster_nr - 1) {
                u32_t next_cluster;

                err = get_next_cluster(xfat, curr_culster, &next_cluster);
                if (err) {
                    file->err = err;
                    return err;
                }

                curr_culster = next_cluster;
            }
        }

        // 然后再从最后一簇分配空间
        err = allocate_free_cluster(file->xfat, curr_culster, cluster_cnt,
                                 &start_free_cluster, &allocated_count, 0, 0);
        if (err) {
            file->err = err;
            return err;
        }

        // 文件大小为0或者curr_cluster无效时均需重新加载
        // 如果文件当前位置为尾簇末端时也需要重新加载
        if (!is_cluster_valid(file->curr_cluster) || is_fpos_cluster_end(file)) {
            file->curr_cluster = start_free_cluster;
        }

        if (!is_cluster_valid(file->start_cluster)) {
            file->start_cluster = start_free_cluster;
        }
    }

    // 最后，再更新文件大小，是否放在关闭文件时进行？
    err = update_file_size(file, size);
    return err;
}

/**
 * 往指定文件中写入数据
 * @param buffer 数据的缓冲
 * @param elem_size 写入的元素字节大小
 * @param count 写入多少个elem_size
 * @param file 待写入的文件
 * @return
 */
xfile_size_t xfile_write(void * buffer, xfile_size_t elem_size, xfile_size_t count, xfile_t * file) {
    xdisk_t * disk = file_get_disk(file);
    u32_t r_count_write = 0;
    xfile_size_t bytes_to_write = count * elem_size;
    xfat_err_t err;
    u8_t * write_buffer = (u8_t *)buffer;

    u32_t cluster_sector = to_sector(disk, to_cluster_offset(file->xfat, file->pos));  // 簇中的扇区偏移
    u32_t sector_offset = to_sector_offset(disk, file->pos);  // 扇区偏移位置

     // 只允许直接写普通文件
    if (file->type != FAT_FILE) {
        file->err = FS_ERR_FSTYPE;
        return 0;
    }

    // 只读性检查
    if (file->attr & XFILE_ATTR_READONLY) {
        file->err = FS_ERR_READONLY;
        return 0;
    }

    // 字节为0，无需写，直接退出
    if (bytes_to_write == 0) {
        file->err = FS_ERR_OK;
        return 0;
    }

    // 当写入量将超过文件大小时，预先分配所有簇，然后再写
    // 后面再写入时，就不必考虑写时文件大小不够的问题了
    if (file->size < file->pos + bytes_to_write) {
        err = expand_file(file, file->pos + bytes_to_write);
        if (err < 0) {
            file->err = err;
            return 0;
        }
    }

    while (bytes_to_write > 0) {
        u32_t curr_write_bytes = 0;
        u32_t sector_count = 0;
        u32_t start_sector;

        start_sector = cluster_fist_sector(file->xfat, file->curr_cluster) + cluster_sector;

        // 起始非扇区边界对齐, 只写取当前扇区
        // 或者起始为0，但写量不超过当前扇区，也只写当前扇区
        // 无论哪种情况，都需要暂存到缓冲区中，然后拷贝到回写到扇区中
        if ((sector_offset != 0) || (!sector_offset && (bytes_to_write < disk->sector_size))) {
            xfat_buf_t * disk_buf;

            sector_count = 1;
            curr_write_bytes = bytes_to_write;

            // 起始偏移非0，如果跨扇区，只写当前扇区
            if (sector_offset != 0) {
                if (sector_offset + bytes_to_write > disk->sector_size) {
                    curr_write_bytes = disk->sector_size - sector_offset;
                }
            }

            // 写整扇区，写入部分到缓冲，最后再回写
            // todo: 连续多次小批量读时，可能会重新加载同一扇区
            err = xfat_bpool_read_sector(disk, &disk_buf, start_sector);
            if (err < 0) {
                file->err = err;
                return 0;
            }

            memcpy(disk_buf->buf + sector_offset, write_buffer, curr_write_bytes);
            err = xfat_bpool_write_sector(disk, disk_buf, start_sector);
            if (err < 0) {
                xdisk_release_buf(disk, disk_buf);
                file->err = err;
                return 0;
            }

            xdisk_release_buf(disk, disk_buf);
            write_buffer += curr_write_bytes;
            bytes_to_write -= curr_write_bytes;
        } else {
            // 起始为0，且写量超过1个扇区，连续写多扇区
            sector_count = to_sector(disk, bytes_to_write);

            // 如果超过一簇，则只写当前簇
            // todo: 这里可以再优化一下，如果簇连写的话，实际是可以连写多簇的
            if ((cluster_sector + sector_count) > file->xfat->sec_per_cluster) {
                sector_count = file->xfat->sec_per_cluster - cluster_sector;
            }

            err = xdisk_write_sector(disk, write_buffer, start_sector, sector_count);
            if (err != FS_ERR_OK) {
                file->err = err;
                return 0;
            }

            curr_write_bytes = sector_count * disk->sector_size;
            write_buffer += curr_write_bytes;
            bytes_to_write -= curr_write_bytes;
        }

        r_count_write += curr_write_bytes;
        file->pos += curr_write_bytes;

        // 如果处于尾簇末端，不需要调整curr_cluster，写完直接退出
        if (is_fpos_cluster_end(file)) {
            break;
        }

        // 其它情况调整写位置, 可重用
        sector_offset += curr_write_bytes;
        if (sector_offset >= disk->sector_size) {
            sector_offset = 0;
            cluster_sector += sector_count;

            if (cluster_sector >= file->xfat->sec_per_cluster) {
                u32_t curr_cluster = file->curr_cluster;

                cluster_sector = 0;
                err = get_next_cluster(file->xfat, curr_cluster, &curr_cluster);
                if (err != FS_ERR_OK) {
                    file->err = err;
                    return 0;
                }

                // 没有尾簇了，到头
                if (!is_cluster_valid(curr_cluster)) {
                    file->err = FS_ERR_OK;
                    return r_count_write / elem_size;;
                }
                file->curr_cluster = curr_cluster;
            }
        }
    }

    file->err = FS_ERR_OK;
    return r_count_write / elem_size;
}

/**
 * 文件是否已经读写到末尾
 * @param file 查询的文件
 * @return
 */
xfat_err_t xfile_eof(xfile_t * file) {
    return (file->pos >= file->size) ? FS_ERR_EOF : FS_ERR_OK;
}

/**
 * 返回当前文件的位置
 * @param file 已经打开的文件
 * @return
 */
xfile_size_t xfile_tell(xfile_t * file) {
    return file->pos;
}

/**
 * 调整文件当前的读写位置
 * @param file 已经打开的文件
 * @param offset 相对于origin指定位置的偏移量
 * @param origin 相对于哪个位置计算偏移量
 * @return
 */
xfat_err_t xfile_seek(xfile_t * file, xfile_ssize_t offset, xfile_orgin_t origin) {
    xfat_err_t err = FS_ERR_OK;

    // 某些移动位置不合法，直接返回
    if (((origin == XFS_SEEK_SET) && (offset < 0))
        || ((origin == XFS_SEEK_END) && (offset > 0))
        || ((origin == XFS_SEEK_CUR) && (file->pos + offset >= file->size))
        || ((origin == XFS_SEEK_CUR) && (file->pos + offset < 0))) {
        return FS_ERR_PARAM;
    }

    // 先处理offset=0这种比较简单的情况
    // offset==0且orgin=END的情况由后面继续处理
    if (offset == 0) {
        if (origin == XFS_SEEK_CUR) {
            return FS_ERR_OK;
        } else if (origin == XFS_SEEK_SET) {
            file->pos = 0;
            file->curr_cluster = file->start_cluster;
            return FS_ERR_OK;
        }
    }

    if (offset > 0) {
        // offset > 0，仅当SET和CUR两种位置，不存在END
        xfile_ssize_t bytes_to_move = offset;
        u32_t current_cluster;
        u32_t current_pos;

        if (origin == XFS_SEEK_SET) {        // SET
            current_pos = 0;
            current_cluster = file->start_cluster;
        } else {    // CUR
            current_pos = file->pos;
            current_cluster = file->curr_cluster;
        }

        // 不要超过文件的末尾，超过报错
        while (bytes_to_move > 0) {
            u32_t curr_move = bytes_to_move;
            u32_t cluster_offset = to_cluster_offset(file->xfat, current_pos);

            // 不超过1簇，直接移动，结束移动
            if (cluster_offset + curr_move < file->xfat->cluster_byte_size) {
                current_pos += curr_move;
                break;
            }

            // 超过当前簇，只在当前簇内移动
            curr_move = file->xfat->cluster_byte_size - cluster_offset;
            current_pos += curr_move;
            bytes_to_move -= curr_move;

            // 进入下一簇: 是否要判断后续簇是否正确？
            err = get_next_cluster(file->xfat, current_cluster, &current_cluster);
            if (err < 0) {
                file->err = err;
                return err;
            }
        }

        file->pos = current_pos;
        file->curr_cluster = current_cluster;
    } else {
        // offset <= 0，仅当END和CUR两种位置，SET情况已经在前面处理
        u32_t cluster_offset;
        xfile_size_t bytes_to_move;
        if (origin == XFS_SEEK_CUR) {
            bytes_to_move = file->pos + offset;
            cluster_offset = to_cluster_offset(file->xfat, file->pos);
        } else {
            bytes_to_move = file->size + offset;
            if (is_fpos_cluster_end(file)) {   // 处理pos正好位于尾簇末端的情况
                cluster_offset = file->xfat->cluster_byte_size;
            } else {
                cluster_offset = to_cluster_offset(file->xfat, file->size);
            }
        }

        // 只在当前簇内移动，直接调整即可
        if ((xfile_ssize_t)cluster_offset + offset >= 0) {
            file->pos += offset;
            return FS_ERR_OK;
        } else {
            u32_t current_cluster = file->start_cluster;
            u32_t current_pos = 0;  // 从最开始的簇移动

            // todo: 簇缓冲区快速查找前几个簇，就不必再重头开始找了
            while (bytes_to_move > 0) {
                u32_t curr_move = bytes_to_move;

                // 不超过1簇，直接移动，结束移动
                if (curr_move < file->xfat->cluster_byte_size) {
                    current_pos += curr_move;
                    break;
                }

                // 超过当前簇，只在当前簇内移动，总是从簇开始位置移动
                curr_move = file->xfat->cluster_byte_size;
                current_pos += curr_move;
                bytes_to_move -= curr_move;

                // 进入下一簇: 是否要判断后续簇是否正确？
                err = get_next_cluster(file->xfat, current_cluster, &current_cluster);
                if (err < 0) {
                    file->err = err;
                    return err;
                }
            }

            file->pos = current_pos;
            file->curr_cluster = current_cluster;
        }
    }
    return FS_ERR_OK;
}

/**
 * 截断文件，使得文件的最终长度比原来的要小
 * @param file 要截断的文件
 * @param size 最终的大小
 * @param mode 截断的模式
 * @return
 */
static xfat_err_t truncate_file(xfile_t * file, xfile_size_t size) {
    xfat_err_t err;
    u32_t pos = 0;
    u32_t curr_cluster = file->start_cluster;

    // 定位到size对应的cluster
    while (pos < size) {
        u32_t next_cluster;

        err = get_next_cluster(file->xfat, curr_cluster, &next_cluster);
        if (err < 0) {
            return err;
        }
        pos += file->xfat->cluster_byte_size;
        curr_cluster = next_cluster;
    }

    // 销毁后继的FAT链
    err = destroy_cluster_chain(file->xfat, curr_cluster);
    if (err < 0) {
        return err;
    }

    if (size == 0) {
        file->start_cluster = CLUSTER_INVALID;
    }

    // 文件截取，当前位置将重置为文件开头，所以直接调整大小即可
    err = update_file_size(file, size);
    return err;
}

/**
 * 调整文件大小。当指定大小小于文件大小时，将截断文件；如果大于，将扩展文件
 * @param file 待调整的文件
 * @param size 调整后的文件大小
 * @param mode 调整模式
 * @return
 */
xfat_err_t xfile_resize (xfile_t * file, xfile_size_t size) {
    xfat_err_t err;

    if (size == file->size) {
        return FS_ERR_OK;
    } else if (size > file->size) {
        err = expand_file(file, size);
        if (err < 0) {
            return err;
        }

        if (!is_cluster_valid(file->curr_cluster)) {
            file->curr_cluster = file->start_cluster;
        }
    } else {
        // 文件小，截断文件
        err = truncate_file(file, size);
        if (err < 0) {
            return err;
        }

        // 如果使得读写位置超出调整后的位置，调整至文件开始处
        if (file->pos >= size) {
            file->pos = 0;
            file->curr_cluster = file->start_cluster;
        }
    }

    return err;
}

/**
 * 获取文件的大小
 * @param file 已经打开的文件
 * @param size 文件大小
 * @return
 */
xfat_err_t xfile_size(xfile_t * file, xfile_size_t * size) {
    *size = file->size;
    return FS_ERR_OK;
}

/**
 * 修改文件的访问时间
 * @param xfat xfat结构
 * @param dir_item 目录项
 * @param arg1
 * @param arg2
 * @return
 */
static xfat_err_t rename_acc(xfat_t * xfat, diritem_t * dir_item, void * arg1, void * arg2) {
    xfat_err_t err;

    // 这种方式只能用于SFN文件项重命名
    const char * new_name = (const char *)arg1;
    err = to_sfn((char *)dir_item->DIR_Name, new_name, DIRITEM_NTRES_ALL_UPPER);

    // 根据文件名的实际情况，重新配置大小写
    dir_item->DIR_NTRes &= ~DIRITEM_NTRES_CASE_MASK;
    dir_item->DIR_NTRes |= get_sfn_case_cfg(new_name);
    return err;
}

/**
 * 修改文件目录项相关信息
 * @param xfat xfat结构
 * @param path 文件或目录的路径
 * @param modify_func 完成修改的回调函数
 * @param arg1 给回调函数使用的参数1，含义由回调函数解释
 * @param arg2 给回调函数使用的参数1，含义由回调函数解释
 * @return
 */
xfat_err_t xfat_modify_diritem(xfat_t *xfat, const char * path, fs_accdir_func_t modify_func,
                               void * arg1, void * arg2) {
    xdisk_t * disk = xfat_get_disk(xfat);
    u32_t curr_cluster = xfat->root_cluster;
    xfat_err_t err;

    while ((path != (const char *)0) && is_cluster_valid(curr_cluster)) {
        u32_t offset = 0;
        int founded = 0;
        u32_t curr_sector_offset = 0;
        xfat_buf_t * disk_buf = 0;

        // 逐个遍历簇
        for (offset = 0; (offset < xfat->cluster_byte_size) && !founded; offset += sizeof(diritem_t)) {
            diritem_t *dir_item;
            u32_t curr_sector = to_phy_sector(xfat, curr_cluster, offset);

            // 一旦遇到扇区边界，读取磁盘
            if (offset % disk->sector_size == 0) {
                // 先释放掉之前的缓存
                xdisk_release_buf(disk, disk_buf);

                err = xfat_bpool_read_sector(disk, &disk_buf, curr_sector);
                if (err < 0) {
                    return err;
                }

                curr_sector_offset = to_sector_addr(disk, offset);
            }

            // 检查空项，结束标记等
            dir_item = (diritem_t *)(disk_buf->buf + (offset - curr_sector_offset));
            if (dir_item->DIR_Name[0] == DIRITEM_NAME_END) {
                xdisk_release_buf(disk, disk_buf);
                return FS_ERR_NONE;
            } else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
                continue;
            }

            // 检查名称是否相同
            if (is_filename_match((const char *) dir_item->DIR_Name, path, 0)) {
                // 当遇到最后一个名称时且相等时，意味着找到文件项
                const char * child_name = get_child_path(path);
                if (child_name == (const char *)0) {
                    diritem_t * end_item;
                    u32_t item_offset;

                    err = modify_func(xfat, dir_item, arg1, arg2);
                    if (err < 0) {
                        xdisk_release_buf(disk, disk_buf);
                        return err;
                    }

                    // 修改完毕之后，要回写
                    err = xfat_bpool_write_sector(disk, disk_buf, curr_sector);
                    xdisk_release_buf(disk, disk_buf);
                    return err;
                } else {
                    // 否则，继续下一个子目录项的比较
                    path = child_name;
                    curr_cluster = (dir_item->DIR_FstClusHI << 16) | dir_item->DIR_FstClusL0;
                    founded = 1;
                    break;
                }
            }
        }
        xdisk_release_buf(disk, disk_buf);

        // 当前簇未找到，继续下一簇
        if (!founded) {
            err = get_next_cluster(xfat, curr_cluster, &curr_cluster);
            if (err < 0) {
                return err;
            }
        }
    }

    return FS_ERR_NONE;
}

/**
 * 重命名文件或目录
 * @param xfat xfat结构
 * @param path 文件或目录的完整路径
 * @param new_name 新文件名，不包含目录
 * @return
 */
xfat_err_t xfile_rename(const char * path, const char * new_name) {
    xfat_err_t err;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    if (!is_sfn_legal(new_name)) {
        return FS_ERR_PARAM;
    }

    err = xfat_modify_diritem(xfat, path, rename_acc, (void *)new_name, 0);
    return err;
}

/**
 * 设置diritem中相应的时间，用作文件时间修改的回调函数
 * @param xfat xfat结构
 * @param dir_item 目录结构项
 * @param arg1 修改的时间类型
 * @param arg2 新的时间
 * @return
 */
static xfat_err_t set_time_acc(xfat_t *xfat, diritem_t *dir_item, void *arg1, void *arg2) {
    xfat_acctime_t type = (xfat_acctime_t)arg1;
    xfile_time_t * time = (xfile_time_t *)arg2;

    switch (type) {
        case XFS_TIME_CTIME:
            dir_item->DIR_CrtDate.year_from_1980 = (u16_t)(time->year - 1980);
            dir_item->DIR_CrtDate.month = time->month;
            dir_item->DIR_CrtDate.day = time->day;
            dir_item->DIR_CrtTime.hour = time->hour;
            dir_item->DIR_CrtTime.minute = time->minute;
            dir_item->DIR_CrtTime.second_2 = (u16_t)(time->second / 2);
            dir_item->DIR_CrtTimeTeenth = (u8_t)(time->second % 2 * 1000 / 100);
            break;
        case XFS_TIME_ATIME:
            dir_item->DIR_LastAccDate.year_from_1980 = (u16_t)(time->year - 1980);
            dir_item->DIR_LastAccDate.month = time->month;
            dir_item->DIR_LastAccDate.day = time->day;
            break;
        case XFS_TIME_MTIME:
            dir_item->DIR_WrtDate.year_from_1980 = (u16_t)(time->year - 1980);
            dir_item->DIR_WrtDate.month = time->month;
            dir_item->DIR_WrtDate.day = time->day;
            dir_item->DIR_WrtTime.hour = time->hour;
            dir_item->DIR_WrtTime.minute = time->minute;
            dir_item->DIR_WrtTime.second_2 = (u16_t)(time->second / 2);
            break;
    }
    return FS_ERR_OK;
}

/**
 * 设置文件的访问时间
 * @param path 文件的完整路径
 * @param time 文件的新访问时间
 * @return
 */
xfat_err_t xfile_set_atime (const char * path, xfile_time_t * time) {
    xfat_err_t err;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    err = xfat_modify_diritem(xfat, path, set_time_acc, (void *) XFS_TIME_ATIME, time);
    return err;
}

/**
 * 设置文件的修改时间
 * @param xfat xfat结构
 * @param path 文件的完整路径
 * @param time 新的文件修改时间
 * @return
 */
xfat_err_t xfile_set_mtime (const char * path, xfile_time_t * time) {
    xfat_err_t err;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    err = xfat_modify_diritem(xfat, path, set_time_acc, (void *) XFS_TIME_MTIME, time);
    return err;
}

/**
 * 设置文件的创建时间
 * @param xfat fsfa结构
 * @param path 文件的完整路径
 * @param time 新的文件创建时间
 * @return
 */
xfat_err_t xfile_set_ctime (const char * path, xfile_time_t * time) {
    xfat_err_t err;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);
    err = xfat_modify_diritem(xfat, path, set_time_acc, (void *) XFS_TIME_CTIME, time);
    return err;
}


/**
 * 文件删除的回调函数
 * @param xfat xfat结构
 * @param dir_item 操作的dir_item
 * @param arg1 删除的是文件（1），还是目录(0)
 * @param arg2 暂未用
 * @return
 */
static xfat_err_t remove_file_acc(xfat_t * xfat, diritem_t * dir_item, void * arg1, void * arg2) {
    xfat_err_t err;
    u8_t is_remove_file = (u8_t)arg1;

    if (is_remove_file && (dir_item->DIR_Attr & DIRITEM_ATTR_DIRECTORY)) {
        return FS_ERR_PARAM;
    }

    // todo: 优化，改成0x00？
    dir_item->DIR_Name[0] = DIRITEM_NAME_FREE;
    err = destroy_cluster_chain(xfat, get_diritem_cluster(dir_item));
    return err;
}

/**
 * 删除指定路径的文件
 * @param xfat xfat结构
 * @param file_path 文件的路径
 * @return
 */
xfat_err_t xfile_rmfile(const char * path) {
    xfat_err_t err;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    err = xfat_modify_diritem(xfat, path, remove_file_acc, (void *)1, (void *)0);
    return err;
}

/**
 * 移除diritem中的项
 * @param file 已经打开的文件
 * @param size 文件大小
 * @return
 */
static xfat_err_t remove_diritem(xfat_t * xfat, u32_t cluster, u32_t offset) {
    xfat_err_t err;
    diritem_t * dir_item;
    xdisk_t * disk = xfat_get_disk(xfat);
    u32_t sector = to_phy_sector(xfat, cluster, offset);
    u32_t sector_offset = to_sector_offset(disk, offset);
    xfat_buf_t * disk_buf;

    err = xfat_bpool_read_sector(disk, &disk_buf, sector);
    if (err < 0) {
        return err;
    }

    // todo: 待优化，名称标记是否为0
    dir_item = (diritem_t *)(disk_buf->buf + sector_offset);
    dir_item->DIR_Name[0] = DIRITEM_NAME_FREE;

    err = xfat_bpool_write_sector(disk, disk_buf, sector);
    if (err < 0) {
        xdisk_release_buf(disk, disk_buf);
        return err;
    }

    xdisk_release_buf(disk, disk_buf);
    return FS_ERR_OK;
}

/**
 * 判断指定目录下是否有子项(子文件)
 * @param file 检查的目录
 * @return
 */
static xfat_err_t dir_has_child(xfile_t * file, int * has_child) {
    xfat_err_t err = FS_ERR_OK;
    xfat_t * xfat = file->xfat;
    xdisk_t * disk = file_get_disk(file);
    u32_t curr_cluster = file->start_cluster;

    while (is_cluster_valid(curr_cluster)) {
        u32_t offset = 0;
        u32_t sector_offset = 0;
        xfat_buf_t * disk_buf = 0;

        // 逐个遍历簇
        for (offset = 0; offset < xfat->cluster_byte_size; offset += sizeof(diritem_t)) {
            diritem_t *dir_item;
            u32_t curr_sector = to_phy_sector(xfat, curr_cluster, offset);

            // 一旦遇到扇区边界，读取磁盘
            if (offset % disk->sector_size == 0) {
                // 先释放之前扇区读取的数据
                xdisk_release_buf(disk, disk_buf);

                err = xfat_bpool_read_sector(disk, &disk_buf, curr_sector);
                if (err < 0) {
                    return err;
                }

                sector_offset = to_sector_addr(disk, offset);
            }

            // 检查空项，结束标记等
            dir_item = (diritem_t *)(disk_buf->buf + (offset - sector_offset));
            if (dir_item->DIR_Name[0] == DIRITEM_NAME_END) {
                break;
            } else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
                continue;
            } else if (is_locate_type_match(dir_item, XFILE_LOCATE_NORMAL)) {
                *has_child = 1;
                xdisk_release_buf(disk, disk_buf);
                return FS_ERR_OK;
            }
        }

        xdisk_release_buf(disk, disk_buf);

        err = get_next_cluster(xfat, curr_cluster, &curr_cluster);
        if (err < 0) {
            return err;
        }
    }

    *has_child = 0;
    return err;
}

/**
 * 删除指定路径的目录(仅能删除目录为空的目录)
 * @param file_path 目录的路径
 * @return
 */
xfat_err_t xfile_rmdir (const char * path) {
    xfat_err_t err;
    xfile_t dir_file;
    int has_child = 0;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    // 先打开dir_path所在的目录
    // todo:改成搜索指令路径的dir_item偏移?
    err = open_sub_file(xfat, xfat->root_cluster, &dir_file, path);
    if (err < 0) {
        return err;
    }

    // 只能打开目录
    if (dir_file.type != FAT_DIR) {
        return FS_ERR_PARAM;
    }

    // 统计子文件或目录数量
    err = dir_has_child(&dir_file, &has_child);
    if (err < 0) {
        return err;
    }

    // 仅能删除目录为空的目录
    if (!has_child) {
        // 根据簇号和偏移定位父目录中，删除指定的diritem来实现目录的删除
        err = remove_diritem(xfat, dir_file.dir_cluster, dir_file.dir_cluster_offset);
        if (err < 0) {
            return err;
        }

        // fix: 忘记删除簇链
        return FS_ERR_OK;
    }

    return FS_ERR_NOT_EMPTY;
}


