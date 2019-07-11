 /**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xfat.h"
#include "xdisk.h"

extern u8_t temp_buffer[512];      // todo: 缓存优化

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
#define to_cluster(xfat, pos)		((pos) / (xfat)->cluster_byte_size)
#define	to_cluseter_count(xfat, size)		(size ? to_cluster(xfat, size) + 1 : 0)


/**
 * 将簇号和簇偏移转换为扇区号
 * @param xfat
 * @param cluster
 * @param cluster_offset
 * @return
 */
u32_t to_phy_sector(xfat_t* xfat, u32_t cluster, u32_t cluster_offset) {
    xdisk_t* disk = xfat_get_disk(xfat);
    return cluster_fist_sector((xfat), (cluster)) + to_sector((disk), (cluster_offset));
}

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

    return  (*s == '\0') && ((*d == '\0') || is_path_sep(*d));
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
 * 初始化FAT项
 * @param xfat xfat结构
 * @param disk_part 分区结构
 * @return
 */
xfat_err_t xfat_mount(xfat_t * xfat, xdisk_part_t * xdisk_part, const char * mount_name) {
    dbr_t * dbr = (dbr_t *)temp_buffer;
    xdisk_t * xdisk = xdisk_part->disk;
    xfat_err_t err;

    xfat->disk_part = xdisk_part;

    // 读取dbr参数区
    err = xdisk_read_sector(xdisk, (u8_t *) dbr, xdisk_part->start_sector, 1);
    if (err < 0) {
        return err;
    }

    // 解析dbr参数中的fat相关信息
    err = parse_fat_header(xfat, dbr);
    if (err < 0) {
        return err;
    }

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
 * 获取指定簇号的第一个扇区编号
 * @param xfat xfat结构
 * @param cluster_no  簇号
 * @return 扇区号
 */
u32_t cluster_fist_sector(xfat_t *xfat, u32_t cluster_no) {
    u32_t data_start_sector = xfat->fat_start_sector + xfat->fat_tbl_sectors * xfat->fat_tbl_nr;
    return data_start_sector + (cluster_no - 2) * xfat->sec_per_cluster;    // 前两个簇号保留
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

    // todo: 优化，一次可否擦除多个扇区
    memset(temp_buffer, erase_state, xdisk->sector_size);
    for (i = 0; i < xfat->sec_per_cluster; i++) {
        xfat_err_t err = xdisk_write_sector(xdisk, temp_buffer, sector + i, 1);
        if (err < 0) {
            return err;
        }
    }
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
    u32_t cluster_count = xfat->fat_tbl_sectors * disk->sector_size / sizeof(cluster32_t);
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

        curr_buffer += xfat->cluster_byte_size;
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
static xfat_err_t to_sfn(char* dest_name, const char* my_name) {
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
    for (i = 0; (i < SFN_LEN) && (*p != '\0') && !is_path_sep(*p); i++) {
        if (ext_existed) {
            if (p == ext_dot) {
                dest = dest_name + 8;
                p++;
                i--;
                continue;
            }
            else if (p < ext_dot) {
                *dest++ = toupper(*p++);
            }
            else {
                *dest++ = toupper(*p++);
            }
        }
        else {
            *dest++ = toupper(*p++);
        }
    }
    return FS_ERR_OK;
}


/**
 * 检查sfn字符串中是否是大写。如果中间有任意小写，都认为是小写
 * @param name
 * @return
 */
static u8_t get_sfn_case_cfg(const char * sfn_name) {
    u8_t case_cfg = 0;

    int name_len;
    const char * src_name = sfn_name;
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
 * 判断两个文件名是否匹配
 * @param name_in_item fatdir中的文件名格式
 * @param my_name 应用可读的文件名格式
 * @return
 */
static u8_t is_filename_match(const char *name_in_dir, const char *to_find_name) {
    char temp_name[SFN_LEN];

    // FAT文件名的比较检测等，全部转换成大写比较
    // 根据目录的大小写配置，将其转换成8+3名称，再进行逐字节比较
    // 但实际显示时，会根据diritem->NTRes进行大小写转换
    to_sfn(temp_name, to_find_name);
    return memcmp(temp_name, name_in_dir, SFN_LEN) == 0;
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
    item->DIR_FstClusL0 = (u16_t )(cluster & 0xFFFF);
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
 * 移动簇的位置
 * @param xfat
 * @param curr_cluster 当前簇号
 * @param curr_offset 当前簇偏移
 * @param move_bytes 移动的字节量（当前只支持本簇及相邻簇内的移动)
 * @param next_cluster 移动后的簇号
 * @param next_offset 移动后的簇偏移
 * @return
 */
xfat_err_t move_cluster_pos(xfat_t* xfat, u32_t curr_cluster, u32_t curr_offset, u32_t move_bytes,
    u32_t* next_cluster, u32_t* next_offset) {
    if ((curr_offset + move_bytes) >= xfat->cluster_byte_size) {
        xfat_err_t err = get_next_cluster(xfat, curr_cluster, next_cluster);
        if (err < 0) {
            return err;
        }

        *next_offset = 0;
    }
    else {
        *next_cluster = curr_cluster;
        *next_offset = curr_offset + move_bytes;
    }

    return FS_ERR_OK;
}

/**
 * 获取下一个有效的目录项
 * @param xfat
 * @param curr_cluster 当前目录项对应的簇号
 * @param curr_offset  当前目录项对应的偏移
 * @param next_cluster 下一目录项对应的簇号
 * @param next_offset  当前目录项对应的簇偏移
 * @param temp_buffer 簇存储的缓冲区
 * @param diritem 下一个有效的目录项
 * @return
 */
xfat_err_t get_next_diritem(xfat_t* xfat, u8_t type, u32_t start_cluster, u32_t start_offset,
    u32_t* found_cluster, u32_t* found_offset, u32_t* next_cluster, u32_t* next_offset,
    u8_t* temp_buffer, diritem_t** diritem) {
    xfat_err_t err;
    diritem_t* r_diritem;

    while (is_cluster_valid(start_cluster)) {
        u32_t sector_offset;
        u32_t curr_sector;

        // 预先取下一位置，方便后续处理
        err = move_cluster_pos(xfat, start_cluster, start_offset, sizeof(diritem_t), next_cluster, next_offset);
        if (err < 0) {
            return err;
        }

        sector_offset = to_sector_offset(xfat_get_disk(xfat), start_offset);
        curr_sector = to_phy_sector(xfat, start_cluster, start_offset);

        // 注意！这里做了修改，课程视频中没有修改。
        // 如果不修改，将导致删除目录树代码有问题。当前效率会低些，但是以后会优化
        err = xdisk_read_sector(xfat_get_disk(xfat), temp_buffer, curr_sector, 1);
        if (err < 0) return err;

        r_diritem = (diritem_t*)(temp_buffer + sector_offset);
        switch (r_diritem->DIR_Name[0]) {
        case DIRITEM_NAME_END:
            if (type & DIRITEM_GET_END) {
                *diritem = r_diritem;
                *found_cluster = start_cluster;
                *found_offset = start_offset;
                return FS_ERR_OK;
            }
            break;
        case DIRITEM_NAME_FREE:
            if (type & DIRITEM_GET_FREE) {
                *diritem = r_diritem;
                *found_cluster = start_cluster;
                *found_offset = start_offset;
                return FS_ERR_OK;
            }
            break;
        default:
            if (type & DIRITEM_GET_USED) {
                *diritem = r_diritem;
                *found_cluster = start_cluster;
                *found_offset = start_offset;
                return FS_ERR_OK;
            }
            break;
        }

        start_cluster = *next_cluster;
        start_offset = *next_offset;
    }

    *diritem = (diritem_t*)0;
    return FS_ERR_EOF;
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
 * @param path 文件或目录的完整路径
 * @param r_diritem 查找到的diritem项
 * @return
 */
static xfat_err_t locate_file_dir_item(xfat_t *xfat, u8_t locate_type, u32_t *dir_cluster, u32_t *cluster_offset,
                                    const char *path, u32_t *move_bytes, diritem_t **r_diritem) {
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

            err = xdisk_read_sector(xdisk, temp_buffer, start_sector + i, 1);
            if (err < 0) {
                return err;
            }

            for (j = initial_offset / sizeof(diritem_t); j < xdisk->sector_size / sizeof(diritem_t); j++) {
                diritem_t *dir_item = ((diritem_t *) temp_buffer) + j;

                if (dir_item->DIR_Name[0] == DIRITEM_NAME_END) {
                    return FS_ERR_EOF;
                } else if (dir_item->DIR_Name[0] == DIRITEM_NAME_FREE) {
                    r_move_bytes += sizeof(diritem_t);
                    continue;
                } else if (!is_locate_type_match(dir_item, locate_type)) {
                    r_move_bytes += sizeof(diritem_t);
                    continue;
                }

                if ((path == (const char *) 0)
                    || (*path == 0)
                    || is_filename_match((const char *) dir_item->DIR_Name, path)) {

                    u32_t total_offset = i * xdisk->sector_size + j * sizeof(diritem_t);
                    *dir_cluster = curr_cluster;
                    *move_bytes = r_move_bytes + sizeof(diritem_t);
                    *cluster_offset = total_offset;
                    if (r_diritem) {
                        *r_diritem = dir_item;
                    }

                    return FS_ERR_OK;
                }

                r_move_bytes += sizeof(diritem_t);
            }
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
            xfat_err_t err = locate_file_dir_item(xfat, XFILE_LOCATE_DOT | XFILE_LOCATE_NORMAL,
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
                file_start_cluster = get_diritem_cluster(dir_item);

                // 如果是..且对应根目录，则cluster值为0，需加载正确的值
                if ((memcmp(dir_item->DIR_Name, DOT_DOT_FILE, SFN_LEN) == 0) && (file_start_cluster == 0)) {
                    file_start_cluster = xfat->root_cluster;
                }
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
    return FS_ERR_OK;
}

/**
 * 打开指定的文件或目录
 * @param xfat xfat结构
 * @param file 打开的文件或目录
 * @param path 文件或目录所在的完整路径，暂不支持相对路径
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

    if (!is_path_end(path)) {
        path = skip_first_path_sep(path);

        // 根目录不存在上级目录
        // 若含有.，直接过滤掉路径
        if (memcmp(path, "..", 2) == 0) {
            return FS_ERR_NONE;
        } else if (memcmp(path, ".", 1) == 0) {
            path++;
        }
    }

    return open_sub_file(xfat, xfat->root_cluster, file, path);
}

/**
 * 在打开的目录下，打开相应的子文件或目录
 * @param dir  已经打开的目录
 * @param sub_file 打开的子文件或目录
 * @param sub_path 以已打开的目录为起点，子文件或目录的完整路径
 * @return
 */
xfat_err_t xfile_open_sub(xfile_t* dir, const char* sub_path, xfile_t* sub_file) {
    if (dir->type != FAT_DIR) {
        return FS_ERR_PARAM;
    }

    if (memcmp(sub_path, ".", 1) == 0) {
        return FS_ERR_PARAM;
    }

    return open_sub_file(dir->xfat, dir->start_cluster, sub_file, sub_path);
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

    cluster_offset = 0;
    err = locate_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL,
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
    err = locate_file_dir_item(file->xfat, XFILE_LOCATE_NORMAL,
            &file->curr_cluster, &cluster_offset, "", &moved_bytes, &dir_item);
    if (err != FS_ERR_OK) {
        return err;
    }

    if (dir_item == (diritem_t *)0) {
        return FS_ERR_EOF;
    }

    file->pos += moved_bytes;

    // 移动位置后，可能超过当前簇，更新当前簇位置
    if (cluster_offset + sizeof(diritem_t) >= file->xfat->cluster_byte_size) {
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

    to_sfn((char *)dir_item->DIR_Name, name);
    set_diritem_cluster(dir_item, cluster);
    dir_item->DIR_FileSize = 0;
    dir_item->DIR_Attr = (u8_t)(is_dir ? DIRITEM_ATTR_DIRECTORY : 0);
    dir_item->DIR_NTRes = get_sfn_case_cfg(name);

    dir_item->DIR_CrtTime.hour = timeinfo.hour;
    dir_item->DIR_CrtTime.minute = timeinfo.minute;
    dir_item->DIR_CrtTime.second_2 = (u16_t)(timeinfo.second / 2);
    dir_item->DIR_CrtTimeTeenth = (u8_t)((timeinfo.second & 1) * 1000);

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
                const char* child_name, u32_t * file_cluster) {
    xfat_err_t err;
    xdisk_t * disk = xfat_get_disk(xfat);
    diritem_t * target_item = (diritem_t *)0;
    u32_t curr_cluster = parent_cluster, curr_offset = 0;
    u32_t free_item_cluster = CLUSTER_INVALID, free_item_offset = 0;
    u32_t file_diritem_sector;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;
	u32_t file_first_cluster = FILE_DEFAULT_CLUSTER;

    // 遍历找到空闲项，在目录末尾添加新项
    do {

        diritem_t* diritem = (diritem_t*)0;
        err = get_next_diritem(xfat, DIRITEM_GET_ALL, curr_cluster, curr_offset,
                                    &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
        if (err < 0) return err;

        if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
            return FS_ERR_NONE;
        }

        if (diritem->DIR_Name[0] == DIRITEM_NAME_END) {        // 有效结束标记
            target_item = diritem;
            break;
        } else if (diritem->DIR_Name[0] == DIRITEM_NAME_FREE) {
            // 空闲项, 还要继续检查，看是否有同名项
            // 记录空闲项的位置
            if (!is_cluster_valid(free_item_cluster)) {
                free_item_cluster = curr_cluster;
                free_item_offset = curr_offset;
            }
        } else if (is_filename_match((const char*)diritem->DIR_Name, child_name)) {
            // 仅名称相同，还要检查是否是同名的文件或目录
            int item_is_dir = diritem->DIR_Attr & DIRITEM_ATTR_DIRECTORY;
            if ((is_dir && item_is_dir) || (!is_dir && !item_is_dir)) { // 同类型且同名
                *file_cluster = get_diritem_cluster(diritem);  // 返回
                return FS_ERR_EXISTED;
            } else {    // 不同类型，即目录-文件同名，直接报错
                return FS_ERR_NAME_USED;
            }
        }

        curr_cluster = next_cluster;
        curr_offset = next_offset;
    } while (1);

    // 如果是目录且不为dot file， 预先分配目录项空间
    if (is_dir && strncmp(".", child_name, 1) && strncmp("..", child_name, 2)) {
        u32_t cluster_count;

        err = allocate_free_cluster(xfat, CLUSTER_INVALID, 1, &file_first_cluster, &cluster_count, 1, 0);
        if (err < 0) return err;

        if (cluster_count < 1) {
            return FS_ERR_DISK_FULL;
        }
	} else {
		file_first_cluster = *file_cluster;
	}

    // 未找到空闲的项，需要为父目录申请新簇，以放置新文件/目录
    if ((target_item == (diritem_t *)0) && !is_cluster_valid(free_item_cluster)) {
        u32_t parent_diritem_cluster;
        u32_t cluster_count;

        xfat_err_t err = allocate_free_cluster(xfat, found_cluster, 1, &parent_diritem_cluster, &cluster_count, 1, 0);
        if (err < 0)  return err;

        if (cluster_count < 1) {
            return FS_ERR_DISK_FULL;
        }

        // 读取新建簇中的第一个扇区，获取target_item
        file_diritem_sector = cluster_fist_sector(xfat, parent_diritem_cluster);
        err = xdisk_read_sector(disk, temp_buffer, file_diritem_sector, 1);
        if (err < 0) {
            return err;
        }
        target_item = (diritem_t *)temp_buffer;     // 获取新簇项
    } else {    // 找到空闲或末尾
        u32_t diritem_offset;
        if (is_cluster_valid(free_item_cluster)) {
            file_diritem_sector = cluster_fist_sector(xfat, free_item_cluster) + to_sector(disk, free_item_offset);
            diritem_offset = free_item_offset;
        } else {
            file_diritem_sector = cluster_fist_sector(xfat, found_cluster) + to_sector(disk, found_offset);
            diritem_offset = found_offset;
        }
        err = xdisk_read_sector(disk, temp_buffer, file_diritem_sector, 1);
        if (err < 0) {
            return err;
        }
        target_item = (diritem_t*)(temp_buffer + to_sector_offset(disk, diritem_offset));     // 获取新簇项
    }

    // 获取目录项之后，根据文件或目录，创建item
    err = diritem_init_default(target_item, disk, is_dir, child_name, file_first_cluster);
    if (err < 0) {
        return err;
    }

    // 写入所在目录项
    err = xdisk_write_sector(disk, temp_buffer, file_diritem_sector, 1);
    if (err < 0) {
        return err;
    }

    *file_cluster = file_first_cluster;
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
    err = create_sub_file(xfat, 1, parent_cluster, name, new_cluster);
    if ((err == FS_ERR_EXISTED) && !fail_on_exist) {
        return FS_ERR_OK;   // 允许文件已存在，则直接退出
    } else if (err < 0) {
        return err;
    }

    // 在新创建的目录下创建文件 . ，其簇号为当前目录的簇号
    dot_cluster = *new_cluster;
    err = create_sub_file(xfat, 1, *new_cluster, ".", &dot_cluster);
    if (err < 0) {
        return err;
    }

    // 分配文件 .. ，其簇号为父目录的簇号
    dot_dot_cluster = parent_cluster;
    err = create_sub_file(xfat, 1, *new_cluster, "..", &dot_dot_cluster);
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
    while (!is_path_end(path)) {
        u32_t new_dir_cluster = FILE_DEFAULT_CLUSTER;
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
            err = create_sub_file(xfat, 0, parent_cluster, path, &file_cluster);
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

static xfat_err_t move_file_pos(xfile_t* file, u32_t move_bytes) {
	u32_t to_move = move_bytes;
	u32_t cluster_offset;

	// 不要超过文件的大小
	if (file->pos + move_bytes >= file->size) {
		to_move = file->size - file->pos;
	}

	// 簇间移动调整，需要调整簇
	cluster_offset = to_cluster_offset(file->xfat, file->pos);
	if (cluster_offset + to_move >= file->xfat->cluster_byte_size) {
		u32_t curr_cluster = file->curr_cluster;

		xfat_err_t err = get_next_cluster(file->xfat, curr_cluster, &curr_cluster);
		if (err != FS_ERR_OK) {
			file->err = err;
			return err;
		}

		if (is_cluster_valid(curr_cluster)) {
			file->curr_cluster = curr_cluster;
		}
	}

	file->pos += to_move;
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
    xdisk_t * disk = file_get_disk(file);
    xfile_size_t r_count_readed = 0;
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

    // 调整读取量，不要超过文件总量
    if (file->pos + bytes_to_read > file->size) {
        bytes_to_read = file->size - file->pos;
    }


    while ((bytes_to_read > 0) && is_cluster_valid(file->curr_cluster)) {
        xfat_err_t err;
        xfile_size_t curr_read_bytes = 0;
        u32_t sector_count = 0;
		u32_t cluster_sector = to_sector(disk, to_cluster_offset(file->xfat, file->pos));  // 簇中的扇区号
		u32_t sector_offset = to_sector_offset(disk, file->pos);  // 扇区偏移位置
		u32_t start_sector = cluster_fist_sector(file->xfat, file->curr_cluster) + cluster_sector;

        // 起始非扇区边界对齐, 只读取当前扇区
        // 或者起始为0，但读取量不超过当前扇区，也只读取当前扇区
        // 无论哪种情况，都需要暂存到缓冲区中，然后拷贝到用户缓冲区
        if ((sector_offset != 0) || (!sector_offset && (bytes_to_read < disk->sector_size))) {
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
            err = xdisk_read_sector(disk, temp_buffer, start_sector, 1);
            if (err < 0) {
                file->err = err;
                return 0;
            }

            memcpy(read_buffer, temp_buffer + sector_offset, curr_read_bytes);
            read_buffer += curr_read_bytes;
            bytes_to_read -= curr_read_bytes;
        } else {
            // 起始为0，且读取量超过1个扇区，连续读取多扇区
            sector_count = (u32_t)to_sector(disk, bytes_to_read);

            // 如果超过一簇，则只读取当前簇
            // todo: 这里可以再优化一下，如果簇连续的话，实际是可以连读多簇的
            if ((cluster_sector + sector_count) > file->xfat->sec_per_cluster) {
                sector_count = file->xfat->sec_per_cluster - cluster_sector;
            }

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

		err = move_file_pos(file, curr_read_bytes);
		if (err) return 0;
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

    // todo: 待优化
    err = xdisk_read_sector(disk, temp_buffer, sector, 1);
    if (err < 0) {
        file->err = err;
        return err;
    }

    dir_item = (diritem_t *)(temp_buffer + offset);
    dir_item->DIR_FileSize = size;

    // 注意更新簇号，因初始文件创建时分配的簇可能并不有效，需要重新设置
    set_diritem_cluster(dir_item, file->start_cluster);
 
    err = xdisk_write_sector(disk, temp_buffer, sector, 1);
    if (err < 0) {
        file->err = err;
        return err;
    }

    file->size = size;
    return FS_ERR_OK;
}

/**
 * 判断文件当前指针是否是结尾簇的末端
 */
static int is_fpos_cluster_end(xfile_t * file) {
    xfile_size_t cluster_offset = to_cluster_offset(file->xfat, file->pos);
    return (cluster_offset == 0) && (file->pos == file->size);
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
    xfat_t * xfat = file->xfat;
	u32_t curr_cluster_cnt = to_cluseter_count(xfat, file->size);
    u32_t expect_cluster_cnt = to_cluseter_count(xfat, size);

    // 当扩充容量需要跨簇时，在簇链之后增加新项
    if (curr_cluster_cnt < expect_cluster_cnt) {
        u32_t cluster_cnt = expect_cluster_cnt - curr_cluster_cnt;
        u32_t start_free_cluster = 0;
		u32_t curr_culster = file->curr_cluster;

        // 先定位至文件的最后一簇, 仅需要定位文件大小不为0的簇
        if (file->size > 0) {
			u32_t next_cluster = file->curr_cluster;

			do {
				curr_culster = next_cluster;

				err = get_next_cluster(xfat, curr_culster, &next_cluster);
				if (err) {
					file->err = err;
					return err;
				}
			} while (is_cluster_valid(next_cluster));
        }

        // 然后再从最后一簇分配空间
        err = allocate_free_cluster(file->xfat, curr_culster, cluster_cnt, &start_free_cluster, 0, 0, 0);
        if (err) {
            file->err = err;
            return err;
        }

		if (!is_cluster_valid(file->start_cluster)) {
			file->start_cluster = start_free_cluster;
			file->curr_cluster = start_free_cluster;
		} else if (!is_cluster_valid(file->curr_cluster) || is_fpos_cluster_end(file)) {
			file->curr_cluster = start_free_cluster;
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

	while ((bytes_to_write > 0) && is_cluster_valid(file->curr_cluster)) {
		u32_t curr_write_bytes = 0;
        u32_t sector_count = 0;

		u32_t cluster_sector = to_sector(disk, to_cluster_offset(file->xfat, file->pos));  // 簇中的扇区偏移
		u32_t sector_offset = to_sector_offset(disk, file->pos);  // 扇区偏移位置
		u32_t start_sector = cluster_fist_sector(file->xfat, file->curr_cluster) + cluster_sector;

        // 起始非扇区边界对齐, 只写取当前扇区
        // 或者起始为0，但写量不超过当前扇区，也只写当前扇区
        // 无论哪种情况，都需要暂存到缓冲区中，然后拷贝到回写到扇区中
        if ((sector_offset != 0) || (!sector_offset && (bytes_to_write < disk->sector_size))) {
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
            err = xdisk_read_sector(disk, temp_buffer, start_sector, 1);
            if (err < 0) {
                file->err = err;
                return 0;
            }

            memcpy(temp_buffer + sector_offset, write_buffer, curr_write_bytes);
            err = xdisk_write_sector(disk, temp_buffer, start_sector, 1);
            if (err < 0) {
                file->err = err;
                return 0;
            }

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

		err = move_file_pos(file, curr_write_bytes);
		if (err) return 0;
    }

    file->err = file->pos == file->size;
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
    xfile_ssize_t final_pos;
    xfile_size_t offset_to_move;
    u32_t curr_cluster, curr_pos;

    // 获取最终的定位位置
    switch (origin) {
    case XFAT_SEEK_SET:
        final_pos = offset;
        break;
    case XFAT_SEEK_CUR:
        final_pos = file->pos + offset;
        break;
    case XFAT_SEEK_END:
        final_pos = file->size + offset;
        break;
    default:
        final_pos = -1;
        break;
    }

    // 超出文件范围
    if ((final_pos < 0) || (final_pos > file->size)) {
        return FS_ERR_PARAM;
    }

    // 相对于当前要调整的偏移量
    offset = final_pos - file->pos;
    if (offset > 0) {
        curr_cluster = file->curr_cluster;
        curr_pos = file->pos;
        offset_to_move = (xfile_size_t)offset;
    } else {
        curr_cluster = file->start_cluster;
        curr_pos = 0;
        offset_to_move = (xfile_size_t)final_pos;
    }

    while (offset_to_move > 0) {
        u32_t cluster_offset = to_cluster_offset(file->xfat, curr_pos);
        xfile_size_t curr_move = offset_to_move;

        // 不超过当前簇
        if (cluster_offset + curr_move < file->xfat->cluster_byte_size) {
            curr_pos += curr_move;
            break;
        }

        // 超过当前簇，只在当前簇内移动
        curr_move = file->xfat->cluster_byte_size - cluster_offset;
        curr_pos += curr_move;
        offset_to_move -= curr_move;

        // 进入下一簇: 是否要判断后续簇是否正确？
        err = get_next_cluster(file->xfat, curr_cluster, &curr_cluster);
        if (err < 0) {
            file->err = err;
            return err;
        }
    }

    file->pos = curr_pos;
    file->curr_cluster = curr_cluster;
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
        file->start_cluster = 0;
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
    xfat_err_t err = FS_ERR_OK;

    if (size == file->size) {
        return FS_ERR_OK;
    } else if (size > file->size) {
        err = expand_file(file, size);
        if (err < 0) {
            return err;
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
 * 文件重命名
 * @param xfat
 * @param path 需要命名的文件完整路径
 * @param new_name 文件新的名称
 * @return
 */
xfat_err_t xfile_rename(const char* path, const char* new_name) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster, curr_offset;
    u32_t next_cluster, next_offset;
    u32_t found_cluster, found_offset;
    const char * curr_path;
    xfat_t * xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);


    curr_cluster = xfat->root_cluster;
    curr_offset = 0;
    for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
        do {
            xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
                    &found_cluster, &found_offset , &next_cluster, &next_offset, temp_buffer, &diritem);
            if (err < 0) {
                return err;
            }

            if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
                return FS_ERR_NONE;
            }

            if (is_filename_match((const char*)diritem->DIR_Name, curr_path)) {
                // 找到，比较下一级子目录
                if (get_file_type(diritem) == FAT_DIR) {
                    curr_cluster = get_diritem_cluster(diritem);
                    curr_offset = 0;
                }
                break;
            }

            curr_cluster = next_cluster;
            curr_offset = next_offset;
        } while (1);
    }

    if (diritem && !curr_path) {
        // 这种方式只能用于SFN文件项重命名
        u32_t dir_sector = to_phy_sector(xfat, found_cluster, found_offset);
        to_sfn((char *)diritem->DIR_Name, new_name);

        // 根据文件名的实际情况，重新配置大小写
        diritem->DIR_NTRes &= ~DIRITEM_NTRES_CASE_MASK;
        diritem->DIR_NTRes |= get_sfn_case_cfg(new_name);

        return xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
    }

    return FS_ERR_OK;
}
/**
 * 设置diritem中相应的时间，用作文件时间修改的回调函数
 * @param xfat xfat结构
 * @param dir_item 目录结构项
 * @param arg1 修改的时间类型
 * @param arg2 新的时间
 * @return
 */
static xfat_err_t set_file_time (xfat_t *xfat, const char * path, stime_type_t time_type, xfile_time_t * time) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster, curr_offset;
    u32_t next_cluster, next_offset;
    u32_t found_cluster, found_offset;
    const char * curr_path;

    curr_cluster = xfat->root_cluster;
    curr_offset = 0;
    for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
        do {
            xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
                &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
            if (err < 0) {
                return err;
            }

            if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
                return FS_ERR_NONE;
            }

            if (is_filename_match((const char*)diritem->DIR_Name, curr_path)) {
                // 找到，比较下一级子目录
                if (get_child_path(curr_path)) {
                    curr_cluster = get_diritem_cluster(diritem);
                    curr_offset = 0;
                }
                break;
            }

            curr_cluster = next_cluster;
            curr_offset = next_offset;
        } while (1);
    }

    if (diritem && !curr_path) {
        // 这种方式只能用于SFN文件项重命名
        u32_t dir_sector = to_phy_sector(xfat, curr_cluster, curr_offset);

        // 根据文件名的实际情况，重新配置大小写
        switch (time_type) {
            case XFAT_TIME_CTIME:
                diritem->DIR_CrtDate.year_from_1980 = (u16_t) (time->year - 1980);
                diritem->DIR_CrtDate.month = time->month;
                diritem->DIR_CrtDate.day = time->day;
                diritem->DIR_CrtTime.hour = time->hour;
                diritem->DIR_CrtTime.minute = time->minute;
                diritem->DIR_CrtTime.second_2 = (u16_t) (time->second / 2);
                diritem->DIR_CrtTimeTeenth = (u8_t) (time->second % 2 * 1000 / 100);
                break;
            case XFAT_TIME_ATIME:
                diritem->DIR_LastAccDate.year_from_1980 = (u16_t) (time->year - 1980);
                diritem->DIR_LastAccDate.month = time->month;
                diritem->DIR_LastAccDate.day = time->day;
                break;
            case XFAT_TIME_MTIME:
                diritem->DIR_WrtDate.year_from_1980 = (u16_t) (time->year - 1980);
                diritem->DIR_WrtDate.month = time->month;
                diritem->DIR_WrtDate.day = time->day;
                diritem->DIR_WrtTime.hour = time->hour;
                diritem->DIR_WrtTime.minute = time->minute;
                diritem->DIR_WrtTime.second_2 = (u16_t) (time->second / 2);
                break;
        }

        return xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
    }

    return FS_ERR_OK;

}

/**
 * 设置文件的访问时间
 * @param xfat xfat结构
 * @param path 文件的完整路径
 * @param time 文件的新访问时间
 * @return
 */
xfat_err_t xfile_set_atime (const char * path, xfile_time_t * time) {
    xfat_err_t err;
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);
    err = set_file_time(xfat, path, XFAT_TIME_ATIME, time);
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
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    err = set_file_time(xfat, path, XFAT_TIME_MTIME, time);
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
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    err = set_file_time(xfat, path, XFAT_TIME_CTIME, time);
    return err;
}

/**
 * 删除指定路径的文件
 * @param xfat xfat结构
 * @param file_path 文件的路径
 * @return
 */
xfat_err_t xfile_rmfile(const char * path) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster, curr_offset;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;
    const char* curr_path;
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    curr_cluster = xfat->root_cluster;
    curr_offset = 0;
    for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
        do {
            xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
                &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
            if (err < 0) {
                return err;
            }

            if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
                return FS_ERR_NONE;
            }

            if (is_filename_match((const char*)diritem->DIR_Name, curr_path)) {
                // 找到，比较下一级子目录
                if (get_child_path(curr_path)) {
                    curr_cluster = get_diritem_cluster(diritem);
                    curr_offset = 0;
                }
                break;
            }

            curr_cluster = next_cluster;
            curr_offset = next_offset;
        } while (1);
    }

    if (diritem && !curr_path) {
        xfat_err_t err;

        // 不允许用此删除目录
        if (diritem->DIR_Attr & DIRITEM_ATTR_DIRECTORY) {
            return FS_ERR_PARAM;
        }

        // 这种方式只能用于SFN文件项重命名
        u32_t dir_sector = to_phy_sector(xfat, found_cluster, found_offset);

        diritem->DIR_Name[0] = DIRITEM_NAME_FREE;

        err = xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
        if (err < 0) return err;

        err = destroy_cluster_chain(xfat, get_diritem_cluster(diritem));
        if (err < 0) return err;

        return FS_ERR_OK;
    }

    return FS_ERR_NONE;
}
/**
 * 判断指定目录下是否有子项(子文件)
 * @param file 检查的目录
 * @return
 */
static xfat_err_t dir_has_child(xfat_t * xfat, u32_t dir_cluster, int * has_child) {
    u32_t curr_cluster = dir_cluster, curr_offset = 0;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;
    diritem_t* diritem = (diritem_t*)0;

    do {
        xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
            &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
        if (err < 0) {
            return err;
        }

        if (diritem == (diritem_t*)0) {
            *has_child = 0;
            break;
        }

        if (is_locate_type_match(diritem, XFILE_LOCATE_NORMAL)) {
            *has_child = 1;
            break;
        }

        curr_cluster = next_cluster;
        curr_offset = next_offset;
    } while (1);

    return FS_ERR_OK;
}

/**
 * 删除指定路径的目录(仅能删除目录为空的目录)
 * @param xfat xfat结构
 * @param file_path 目录的路径
 * @return
 */
xfat_err_t xfile_rmdir (const char * path) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster, curr_offset;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;
    const char* curr_path;
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    // 定位path所对应的位置和dirite m
    curr_cluster = xfat->root_cluster;
    curr_offset = 0;
    for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
        do {
            xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
                &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
            if (err < 0) {
                return err;
            }

            if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
                return FS_ERR_NONE;
            }

            if (is_filename_match((const char*)diritem->DIR_Name, curr_path)) {
                // 找到，比较下一级子目录
                if (get_child_path(curr_path)) {
                    curr_cluster = get_diritem_cluster(diritem);
                    curr_offset = 0;
                }
                break;
            }

            curr_cluster = next_cluster;
            curr_offset = next_offset;
        } while (1);
    }

    if (diritem && !curr_path) {
        xfat_err_t err;
        int has_child;
        u32_t dir_sector;

        if (get_file_type(diritem) != FAT_DIR) {
            return FS_ERR_PARAM;
        }

        dir_sector = to_phy_sector(xfat, found_cluster, found_offset);
        err = dir_has_child(xfat, get_diritem_cluster(diritem), &has_child);
        if (err < 0) return err;

        if (has_child) {
            return FS_ERR_NOT_EMPTY;
        }

        // dir_has_child会破坏缓冲区，所以这里重新加载一遍
        err = xdisk_read_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
        if (err < 0) return err;

        diritem = (diritem_t*)(temp_buffer + to_sector_offset(xfat_get_disk(xfat), found_offset));
        diritem->DIR_Name[0] = DIRITEM_NAME_FREE;

        err = xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
        if (err < 0) return err;

        err = destroy_cluster_chain(xfat, get_diritem_cluster(diritem));
        if (err < 0) return err;

        return FS_ERR_OK;
    }

    return FS_ERR_NONE;
}

/**
 * 递归删除所有子文件或目录
 * @param xfat
 * @param parent_cluster
 * @return
 */
static xfat_err_t rmdir_all_children(xfat_t* xfat, u32_t parent_cluster) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster = parent_cluster, curr_offset = 0;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;

    do {
        xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
            &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
        if (err < 0) {
            return err;
        }

        if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
            return FS_ERR_OK;
        }

        if (diritem->DIR_Name[0] == DIRITEM_NAME_END) {
            return FS_ERR_OK;
        }

        if (is_locate_type_match(diritem, XFILE_LOCATE_NORMAL)) {
            u32_t dir_cluster = get_diritem_cluster(diritem);
            u32_t dir_sector = to_phy_sector(xfat, found_cluster, found_offset);

            diritem->DIR_Name[0] = DIRITEM_NAME_FREE;
            err = xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
            if (err < 0) return err;

            if (get_file_type(diritem) == FAT_DIR) {
                // 这里可能会改缓存，所以最好使用dir_cluster
                err = rmdir_all_children(xfat, dir_cluster);
                if (err < 0) return err;
            }

            err = destroy_cluster_chain(xfat, dir_cluster);
            if (err < 0) return err;
        }

        curr_cluster = next_cluster;
        curr_offset = next_offset;
    } while (1);

    return FS_ERR_OK;
}

/**
 * 删除指定路径的目录(仅能删除目录为空的目录)
 * @param xfat xfat结构
 * @param file_path 目录的路径
 * @return
 */
xfat_err_t xfile_rmdir_tree(const char* path) {
    diritem_t* diritem = (diritem_t*)0;
    u32_t curr_cluster, curr_offset;
    u32_t found_cluster, found_offset;
    u32_t next_cluster, next_offset;
    const char* curr_path;
    xfat_t* xfat;

    // 根据名称解析挂载结构
    xfat = xfat_find_by_name(path);
    if (xfat == (xfat_t *)0) {
        return FS_ERR_NOT_MOUNT;
    }

    path = get_child_path(path);

    // 定位path所对应的位置和diritem
    curr_cluster = xfat->root_cluster;
    curr_offset = 0;
    for (curr_path = path; curr_path != '\0'; curr_path = get_child_path(curr_path)) {
        do {
            xfat_err_t err = get_next_diritem(xfat, DIRITEM_GET_USED, curr_cluster, curr_offset,
                &found_cluster, &found_offset, &next_cluster, &next_offset, temp_buffer, &diritem);
            if (err < 0) {
                return err;
            }

            if (diritem == (diritem_t*)0) {    // 已经搜索到目录结束
                return FS_ERR_NONE;
            }

            if (is_filename_match((const char*)diritem->DIR_Name, curr_path)) {
                // 找到，比较下一级子目录
                if (get_child_path(curr_path)) {
                    curr_cluster = get_diritem_cluster(diritem);
                    curr_offset = 0;
                }
                break;
            }

            curr_cluster = next_cluster;
            curr_offset = next_offset;
        } while (1);
    }

    if (diritem && !curr_path) {
        xfat_err_t err;
        u32_t dir_sector;
        u32_t diritem_cluster = get_diritem_cluster(diritem);

        if (get_file_type(diritem) != FAT_DIR) {
            return FS_ERR_PARAM;
        }

        dir_sector = to_phy_sector(xfat, found_cluster, found_offset);
        diritem->DIR_Name[0] = DIRITEM_NAME_FREE;
        err = xdisk_write_sector(xfat_get_disk(xfat), temp_buffer, dir_sector, 1);
        if (err < 0) return err;

        err = rmdir_all_children(xfat, diritem_cluster);
        if (err < 0) return err;

        err = destroy_cluster_chain(xfat, diritem_cluster);
        if (err < 0) return err;

        return FS_ERR_OK;
    }

    return FS_ERR_NONE;
}

/**
 * 关闭已经打开的文件
 * @param file 待关闭的文件
 * @return
 */
xfat_err_t xfile_close(xfile_t *file) {
    // 在这里，更新文件的访问时间，写时间和扩容和的增量，属性值的修改等
    return FS_ERR_OK;
}
