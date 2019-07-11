
#include <stdio.h>
#include "xdisk.h"
#include "xfat.h"

extern xdisk_driver_t vdisk_driver;

const char * disk_path_test = "../disk_test.img";
const char * disk_path = "../disk.img";

static u32_t write_buffer[160*1024];
static u32_t read_buffer[160*1024];

xdisk_t disk;
xdisk_part_t disk_part;
xfat_t xfat;

// 磁盘缓存读写测试
int disk_buf_test(xdisk_t * disk, int buf_nr) {
    xfat_err_t err;
    xdisk_buf_t * disk_buf;
    int i;

    // 连续读写测试，全部使用缓存
    for (i = 0; i < buf_nr; i++) {
        err = xdisk_buf_read_sector(disk, &disk_buf, i);
        if (err < 0) return err;

        memset(disk_buf->buf, i, disk->sector_size);

        err = xdisk_buf_write_sector(disk, disk_buf, i);
        if (err < 0) return err;

        err = xdisk_release_buf(disk, disk_buf);
        if (err < 0) return err;
    }

    for (i = 0; i < buf_nr; i++) {
        err = xdisk_buf_read_sector(disk, &disk_buf, i);
        if (err < 0) return err;

        memset(disk_buf->buf, i, disk->sector_size);

        err = xdisk_buf_write_sector(disk, disk_buf, i);
        if (err < 0) return err;

        err = xdisk_release_buf(disk, disk_buf);
        if (err < 0) return err;
    }
    for (i = 0; i < buf_nr; i++) {
        err = xdisk_buf_read_sector(disk, &disk_buf, i + buf_nr);
        if (err < 0) return err;

        memset(disk_buf->buf, i + buf_nr, disk->sector_size);

        err = xdisk_buf_write_sector(disk, disk_buf, i + buf_nr);
        if (err < 0) return err;

        err = xdisk_release_buf(disk, disk_buf);
        if (err < 0) return err;
    }

    xdisk_flush_all(disk);

    return 0;
}

// io测试，测试通过要注意关掉
int disk_io_test (void) {
    int i, err;
    xdisk_t disk_test;

#define DISK_BUF_NR     3
    static u8_t disk_buf[DISK_BUF_SIZE(512, DISK_BUF_NR)];

    disk_test.driver = &vdisk_driver;
    disk_test.sector_size = 512;

    memset(read_buffer, 0, sizeof(read_buffer));

    err = xdisk_open(&disk_test, "vidsk_test", &vdisk_driver, (void *)disk_path_test, disk_buf, sizeof(disk_buf));
    if (err) {
        printf("open disk failed!\n");
        return -1;
    }

    err = xdisk_write_sector(&disk_test, (u8_t *)write_buffer, 0, 2);
    if (err) {
        printf("disk write failed!\n");
        return -1;
    }

    err = xdisk_read_sector(&disk_test, (u8_t *)read_buffer, 0, 2);
    if (err) {
        printf("disk read failed!\n");
        return -1;
    }

    err = memcmp((u8_t *)read_buffer, (u8_t *)write_buffer, disk_test.sector_size * 2);
    if (err != 0) {
        printf("data no equal!\n");
        return -1;
    }

    // 磁盘缓存读写测试
    err = disk_buf_test(&disk_test, DISK_BUF_NR);
    if (err < 0) {
        printf("disk cache test failed!\n");
        return -1;
    }

    err = xdisk_close(&disk_test);
    if (err) {
        printf("disk close failed!\n");
        return -1;
    }

    printf("disk io test ok!\n");
    return 0;
}

int disk_part_test (void) {
    u32_t count, i;
    xfat_err_t err = FS_ERR_OK;

    printf("partition read test...\n");

    err = xdisk_get_part_count(&disk, &count);
    if (err < 0) {
        printf("partion count detect failed!\n");
        return err;
    }
    printf("partition count:%d\n", count);

	for (i = 0; i < count; i++) {
		xdisk_part_t part;
		int err;

		err = xdisk_get_part(&disk, &part, i);
		if (err == -1) {
			printf("read partion in failed:%d\n", i);
			return -1;
		}

        printf("no %d: start: %d, count: %d, capacity:%.0f M\n",
               i, part.start_sector, part.total_sector,
               part.total_sector * disk.sector_size / 1024 / 1024.0);
    }
    return 0;
}

void show_dir_info (diritem_t * diritem) {
    char file_name[12];
    u8_t attr = diritem->DIR_Attr;

    // name
    memset(file_name, 0, sizeof(file_name));
    memcpy(file_name, diritem->DIR_Name, 11);
    if (file_name[0] == 0x05) {
        file_name[0] = 0xE5;
    }
    printf("\n name: %s, ", file_name);

    // attr
    printf("\n\t");
    if (attr & DIRITEM_ATTR_READ_ONLY) {
        printf("readonly, ");
    }

    if (attr & DIRITEM_ATTR_HIDDEN) {
        printf("hidden, ");
    }

    if (attr & DIRITEM_ATTR_SYSTEM) {
        printf("system, ");
    }

    if (attr & DIRITEM_ATTR_DIRECTORY) {
        printf("directory, ");
    }

    if (attr & DIRITEM_ATTR_ARCHIVE) {
        printf("achinve, ");
    }

    // create time
    printf("\n\tcreate:%d-%d-%d, ", diritem->DIR_CrtDate.year_from_1980 + 1980,
            diritem->DIR_CrtDate.month, diritem->DIR_CrtDate.day);
    printf("\n\time:%d-%d-%d, ", diritem->DIR_CrtTime.hour, diritem->DIR_CrtTime.minute,
           diritem->DIR_CrtTime.second_2 * 2 + diritem->DIR_CrtTimeTeenth / 100);

    // last write time
    printf("\n\tlast write:%d-%d-%d, ", diritem->DIR_WrtDate.year_from_1980 + 1980,
           diritem->DIR_WrtDate.month, diritem->DIR_WrtDate.day);
    printf("\n\ttime:%d-%d-%d, ", diritem->DIR_WrtTime.hour,
           diritem->DIR_WrtTime.minute, diritem->DIR_WrtTime.second_2 * 2);

    // last acc time
    printf("\n\tlast acc:%d-%d-%d, ", diritem->DIR_LastAccDate.year_from_1980 + 1980,
           diritem->DIR_LastAccDate.month, diritem->DIR_LastAccDate.day);

    // size
    printf("\n\tsize %d kB, ", diritem->DIR_FileSize / 1024);
    printf("\n\tcluster %d, ", (diritem->DIR_FstClusHI << 16) | diritem->DIR_FstClusL0);

    printf("\n");
}

int fat_dir_test(void) {
    int err;
    u32_t curr_cluster;
    u8_t * culster_buffer;
    int index = 0;
    diritem_t * dir_item;
    u32_t j;

    printf("root dir read test...\n");

    culster_buffer = (u8_t *)malloc(xfat.cluster_byte_size);

    // 解析根目录所在的簇
    curr_cluster = xfat.root_cluster;
    while (is_cluster_valid(curr_cluster)) {
        err = read_cluster(&xfat, culster_buffer, curr_cluster, 1);
        if (err) {
            printf("read cluster %d failed\n", curr_cluster);
            return -1;
        }

        dir_item = (diritem_t *)culster_buffer;
        for (j = 0; j < xfat.cluster_byte_size / sizeof(diritem_t); j++) {
            u8_t  * name = (u8_t *)(dir_item[j].DIR_Name);
            if (name[0] == DIRITEM_NAME_FREE) {
                continue;
            } else if (name[0] == DIRITEM_NAME_END) {
                break;
            }

            index++;
            printf("no: %d, ", index);
            show_dir_info(&dir_item[j]);
        }

        err = get_next_cluster(&xfat, curr_cluster, &curr_cluster);
        if (err) {
            printf("get next cluster failed， current cluster %d\n", curr_cluster);
            return -1;
        }
    }

    return 0;
}

int fat_file_test(void) {
    int err;
    u32_t curr_cluster;
    u8_t * culster_buffer;
    int size = 0;

    printf("root dir read test...\n");

    culster_buffer = (u8_t *)malloc(xfat.cluster_byte_size + 1);

    // 从fat_dir_test选择1个文件的cluster起始号，根据测试情况修改
    curr_cluster = 11459;
    while (is_cluster_valid(curr_cluster)) {
        err = read_cluster(&xfat, culster_buffer, curr_cluster, 1);
        if (err) {
            printf("read cluster %d failed\n", curr_cluster);
            return -1;
        }

        // print file content
        culster_buffer[xfat.cluster_byte_size + 1] = '\0';
        printf("%s", (char *)culster_buffer);

        size += xfat.cluster_byte_size;
        err = get_next_cluster(&xfat, curr_cluster, &curr_cluster);
        if (err) {
            printf("get next cluster failed， current cluster %d\n", curr_cluster);
            return -1;
        }
    }

    printf("\nfile size:%d\n", size);
    return 0;
}

void show_file_info(xfileinfo_t * fileinfo) {
    printf("\n\nname: %s, ", fileinfo->file_name);
    switch (fileinfo->type) {
        case FAT_FILE:
            printf("file, ");
            break;
        case FAT_DIR:
            printf("dir, ");
            break;
        case FAT_VOL:
            printf("vol, ");
            break;
        default:
            printf("unknown, ");
            break;
    }

    // create time
    printf("\n\tcreate:%d-%d-%d, ", fileinfo->create_time.year, fileinfo->create_time.month, fileinfo->create_time.day);
    printf("\n\ttime:%d-%d-%d, ", fileinfo->create_time.hour, fileinfo->create_time.minute, fileinfo->create_time.second);

    // last write time
    printf("\n\tlast write:%d-%d-%d, ", fileinfo->modify_time.year, fileinfo->modify_time.month, fileinfo->modify_time.day);
    printf("\n\ttime:%d-%d-%d, ", fileinfo->modify_time.hour, fileinfo->modify_time.minute, fileinfo->modify_time.second);

    // last acc time
    printf("\n\tlast acc:%d-%d-%d, ", fileinfo->last_acctime.year, fileinfo->last_acctime.month, fileinfo->last_acctime.day);

    // size
    printf("\n\tsize %d kB, ", fileinfo->size / 1024);

    printf("\n");
}

int list_sub_files (xfile_t * file, int curr_depth) {
    int err = 0;
    int i;
    xfileinfo_t fileinfo;

    err = xdir_first_file(file, &fileinfo);
    if (err)  return err;

    do {
        xfile_t sub_file;

        if (fileinfo.type == FAT_DIR) {
            for (i = 0; i < curr_depth; i++) {
                printf("-");
            }
            printf("%s\n", fileinfo.file_name);

            err = xfile_open_sub(file, &sub_file, fileinfo.file_name);
            if (err < 0) {
                return err;
            }

            err = list_sub_files(&sub_file, curr_depth + 1);
            if (err < 0) {
                return err;
            }
        } else {
            for (i = 0; i < curr_depth; i++) {
                printf("-");
            }
            printf("%s\n", fileinfo.file_name);
        }

    } while ((err = xdir_next_file(file, &fileinfo)) == 0);

    return err;
}

int dir_trans_test(void) {
    xfile_t top_dir;
    xfileinfo_t fileinfo;
    int err;

    printf("\ntrans dir test!\n");

    // 仅遍历根目录下面的这一层
    err = xfile_open(&top_dir, "/mp0");
    if (err < 0) {
        printf("open directory failed!\n");
        return -1;
    }

    err = xdir_first_file(&top_dir, &fileinfo);
    if (err < 0) {
        printf("get file info failed!\n");
        return -1;
    }
    show_file_info(&fileinfo);

    while ((err = xdir_next_file(&top_dir, &fileinfo)) == 0) {
        show_file_info(&fileinfo);
    }
    if (err < 0) {
        printf("get file info failed!\n");
        return -1;
    }

    err = list_sub_files(&top_dir, 0);
    if (err < 0) {
        printf("list file failed!\n");
        return -1;
    }

    err = xfile_close(&top_dir);
    if (err < 0) {
        printf("close file failed!\n");
        return -1;
    }

    printf("file trans test ok\n");
    return 0;
}

int file_read_all_test(const char * path, xfile_size_t elem_size,  xfile_size_t e_count) {
    xfile_t file;
    xfile_size_t readed_count;
    xfile_size_t curr_offset = 0;

    xfat_err_t err = xfile_open(&file, path);
    if (err != FS_ERR_OK) {
        printf("open file failed! %s\n", path);
        return -1;
    }

    while ((readed_count = xfile_read(read_buffer, elem_size, e_count, &file)) > 0) {
        u32_t i = 0;
        u32_t num_start = curr_offset / 4;       // 起始数值
        u32_t bytes_count = readed_count * elem_size;    // 总的字节数
        for (i = 0; i < bytes_count; i += 4) {
            if (read_buffer[i / 4] != num_start++) {
                printf("read file failed!\n");
                return -1;
            }
        }

        curr_offset += bytes_count;
    }

    if (xfile_error(&file) < 0) {
        printf("read failed!\n");
        return -1;
    }

    return FS_ERR_OK;
}

int fs_read_test (void) {
    const char * file_0b_path = "/mp0/read/0b.bin";
    const char * file_1MB_path = "/mp0/read/1MB.bin";
    xfile_t file_bin;
    xfat_err_t err;

    printf("\nfile read test!\n");

    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_0b_path, 32, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 不超过一个扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_1MB_path, disk.sector_size - 32, 2);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 刚好一个扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_1MB_path, disk.sector_size, 2);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 跨扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_1MB_path, disk.sector_size + 14, 2);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 刚好超过一个簇的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_1MB_path, xfat.cluster_byte_size + 32, 2);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 跨多个簇的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_all_test(file_1MB_path, 2 * xfat.cluster_byte_size + 32, 2);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    printf("\nfile read test ok\n");
    return FS_ERR_OK;
}

int _fs_seek_test(xfile_t * file, xfile_orgin_t orgin, xfile_ssize_t offset) {
    int err = 0;
    u32_t target_pos;
    int count,  value;

    switch (orgin) {
        case XFS_SEEK_SET:
            target_pos = offset;
            break;
        case XFS_SEEK_END:
            target_pos = file->size + offset;
            break;
        case XFS_SEEK_CUR:
            target_pos = file->pos + offset;
            break;
    }

    err = xfile_seek(file, offset, orgin);
    if (err) {
        printf("seek error\n");
        return err;
    }

    if (xfile_tell(file) != target_pos) {
        printf("seek error\n");
        return -1;
    }

    switch (target_pos % 4) {
        case 0:
            value = target_pos / 4;
            break;
        case 1:
            value = target_pos / 4;
            value = (value >> 8) | ((value + 1) << 24);
            break;
        case 2:
            value = target_pos / 4;
            value = (value >> 16) | ((value + 1) << 16);
            break;
        case 3:
            value = target_pos / 4;
            value = (value >> 24) | ((value + 1) << 8);
            break;
    }

    count = xfile_read(read_buffer, 4, 1, file);
    if (count < 1) {
        printf("seek error\n");
        return err;
    }

    if (*(u32_t *)read_buffer != value){
        printf("seek error\n");
        return -1;
    }
    return 0;
}

int fs_seek_test(void) {
    xfat_err_t err;
    xfile_t file;

    printf("\nfile seek test!\n");

    err = xfile_open(&file, "/mp0/seek/1MB.bin");
    if (err != FS_ERR_OK) {
        printf("open file failed!\n");
        return -1;
    }

    err = _fs_seek_test(&file, XFS_SEEK_SET, 32);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_SET, 576);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_SET, 4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_SET, -1);
    if (err == FS_ERR_OK) return -1;

    err = _fs_seek_test(&file, XFS_SEEK_CUR, 32);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, 576);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, 4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, -32);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, -512);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, -1024);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_CUR, -0xFFFFFFF);
    if (err == FS_ERR_OK) return err;

    err = _fs_seek_test(&file, XFS_SEEK_END, -32);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_END, -576);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_END, -4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFS_SEEK_END, 32);
    if (err == FS_ERR_OK) return err;

    return 0;
}

int fs_open_test (void) {
    const char * not_exist_path = "/mp0/file_not_exist.txt";
    const char * exist_path = "/mp0/12345678ABC";    // 注意：文件名要大写
    const char * file1 = "/mp0/open/file.txt";
    const char * file2 = "/mp0/open/a0/a1/a2/a3/a4/a5/a6/a7/a8/a9/a10/a11/a12/a13/a14/a15/a16/a17/a18/a19/file.txt";
    xfat_err_t err;
    xfile_t file;

    printf("fs_open test...\n");

    err = xfile_open(&file, "/mp0");
    if (err) {
        printf("open file failed %s!\n", "/");
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&file, not_exist_path);
    if (err == 0) {
        printf("open file ok %s!\n", not_exist_path);
        return -1;
    }

    err = xfile_open(&file, exist_path);
    if (err < 0) {
        printf("open file failed %s!\n", exist_path);
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&file, file1);
    if (err < 0) {
        printf("open file failed %s!\n", file1);
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&file, file2);
    if (err < 0) {
        printf("open file failed %s!\n", file2);
        return -1;
    }
    xfile_close(&file);

    printf("file open test ok\n");
    return 0;
}

xfat_err_t fs_modify_file_test(void) {
    xfat_err_t err;
    xfile_t file;
    const char * dir_path = "/mp0/modify/a0/a1/a2/";
    const char file_name1[] = "ABC.efg";
    const char file_name2[] = "efg.ABC";
    const char * new_name;
    char curr_path[64];
    xfile_time_t timeinfo;

    printf("modify file attr test...\n");

    printf("\n Before rename:\n");

    // 显示原目录下的文件
    err = xfile_open(&file, dir_path);
    if (err < 0) {
        printf("Open dir failed!\n");
        return err;
    }

    err = list_sub_files(&file, 0);
    if (err < 0) {
        return err;
    }
    xfile_close(&file);

    // 尝试打开其中一个路径，判断如何命名
    sprintf(curr_path, "%s%s", dir_path, file_name1);
    err = xfile_open(&file, curr_path);
    if (err < 0) {
        // 打开文件1失败，则当前文件2存在，新名称为文件1名称
        sprintf(curr_path, "%s%s", dir_path, file_name2);
        new_name = file_name1;
    } else {
        sprintf(curr_path, "%s%s", dir_path, file_name1);
        new_name = file_name2;
    }

    // 文件重命名
    err = xfile_rename(curr_path, new_name);
    if (err < 0) {
        printf("rename failed: %s -- to -- %s\n", curr_path, new_name);
        return err;
    }
    xfile_close(&file);

    printf("\n After rename:\n");

    sprintf(curr_path, "%s%s", dir_path, new_name);

    // 修改文件时间
    timeinfo.year = 2030;
    timeinfo.month = 10;
    timeinfo.day = 12;
    timeinfo.hour = 13;
    timeinfo.minute = 32;
    timeinfo.second = 12;
    timeinfo.mil_second = 20;
    err = xfile_set_atime(curr_path, &timeinfo);
    if (err < 0) {
        printf("set acc time failed!\n");
        return err;
    }

    timeinfo.year = 2031;
    timeinfo.month = 11;
    timeinfo.day = 13;
    timeinfo.hour = 14;
    timeinfo.minute = 33;
    timeinfo.second = 13;
    timeinfo.mil_second = 21;
    err = xfile_set_mtime(curr_path, &timeinfo);
    if (err < 0) {
        printf("set modify time failed!\n");
        return err;
    }

    timeinfo.year = 2032;
    timeinfo.month = 12;
    timeinfo.day = 14;
    timeinfo.hour = 15;
    timeinfo.minute = 35;
    timeinfo.second = 14;
    timeinfo.mil_second = 22;
    err = xfile_set_ctime(curr_path, &timeinfo);
    if (err < 0) {
        printf("set create time failed!\n");
        return err;
    }

    // 重命名后，列表显示所有文件，显示命名状态
    err = xfile_open(&file, dir_path);
    if (err < 0) {
        printf("Open dir failed!\n");
        return err;
    }

    err = list_sub_files(&file, 0);
    if (err < 0) {
        return err;
    }
    xfile_close(&file);

    return FS_ERR_OK;
}

int file_write_test(const char * path, u32_t elem_size, u32_t elem_count, u32_t write_count) {
    int i, j;
    xfat_err_t err;
    xfile_t file;

    err = xfile_open(&file, path);
    if (err < 0) {
        printf("Open failed:%s\n", path);
        return err;
    }

    // 连续多次测试写入
    for (i = 0; i < write_count; i++) {
        // 从预先指定的write_buffer中取出数据写入文件
        err = xfile_write(write_buffer, elem_size, elem_count, &file);
        if (err < 0) {
            printf("Write failed\n");
            return err;
        }

        // 再定位到文件开始处
        err = xfile_seek(&file, -(xfile_ssize_t)(elem_size * elem_count), XFS_SEEK_CUR);
        if (err < 0) {
            printf("seek failed\n");
            return err;
        }

        // 读取比较，检查是否完全写入
        memset(read_buffer, 0, sizeof(read_buffer));
        err = xfile_read(read_buffer, elem_size, elem_count, &file);
        if (err < 0) {
            printf("read failed\n");
            return err;
        }

        for (j = 0; j < elem_size * elem_count / sizeof(u32_t); j++) {
            if (read_buffer[j] != j) {
                printf("content different!\n");
                return -1;
            }
        }
    }

    xfile_close(&file);
    return 0;
}

int fs_write_test (void) {
    const char * dir_path = "/mp0/write/";
    char file_path[64];
    xfat_err_t err;

    printf("Write file test!\n");

    sprintf(file_path, "%s%s", dir_path, "1MB.bin");
    err = file_write_test(file_path, 32, 64, 5);     // 不到一个扇区，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    err = file_write_test(file_path, disk.sector_size, 12, 5);     // 扇区边界写，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    err = file_write_test(file_path, disk.sector_size + 32, 12, 5);     // 超过1个扇区，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    err = file_write_test(file_path, xfat.cluster_byte_size, 12, 5);     // 簇边界写，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    err = file_write_test(file_path, xfat.cluster_byte_size + 32, 12, 5);     // 超过1个簇，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    err = file_write_test(file_path, 3 * xfat.cluster_byte_size + 32, 12, 5);     // 超过多个簇，且非扇区边界对齐的写
    if (err < 0) {
        printf("write file failed!\n");
        return err;
    }

    // 扩容写测试
    do {
        xfile_t file;
        u32_t elem_size = 3 * xfat.cluster_byte_size + 32;
        u32_t elem_count = 2;
        u32_t write_count = sizeof(write_buffer) / elem_count / elem_size;  // 注意，调整写入使得可全部写入
        xfile_size_t size = elem_size * elem_count * write_count;
        xfile_size_t file_size;
        u32_t i;

        printf("\n expand write file!\n");

        sprintf(file_path, "%s%s", dir_path, "32KB.bin");
        err = file_write_test(file_path, elem_size, elem_count, write_count);     // 超过多个簇，且非扇区边界对齐的写
        if (err < 0) {
            printf("write file failed!\n");
            return err;
        }

        // 检查文件写入后大小
        err = xfile_open(&file, file_path);
        if (err < 0) {
            printf("Open failed:%s\n", file_path);
            return err;
        }

        xfile_size(&file, &file_size);
        if (file_size != size) {
            printf("Write failed:%s\n", file_path);
            return err;
        }

        // 检查文件内容
        for (i = 0; i < write_count; i++) {
            u32_t j;

            memset(read_buffer, 0, sizeof(read_buffer));
            err = xfile_read(read_buffer, elem_size, elem_count, &file);
            if (err < 0) {
                printf("read failed\n");
                return err;
            }

            for (j = 0; j < elem_size * elem_count / sizeof(u32_t); j++) {
                if (read_buffer[j] != j) {
                    printf("content different!\n");
                    return -1;
                }
            }
        }

        xfile_close(&file);

    } while (0);

    printf("Write file test end!\n");
    return 0;
}

xfat_err_t fs_create_test (void) {
    xfat_err_t err = FS_ERR_OK;
    const char * dir_path = "/mp0/create/c0/c1/c2/c3/c4/c5/c6/c7/c8/c9";  // 这个路径可配置短一些
    char path[256];
    int i, j;

    printf("create test\n");

    // 注意，如果目录下已经有要创建的文件
    // 或者之前在调试该代码时，已经执行了一部分导致部分文件被创建时
    // 注意在重启调试前，先清除干净根目录下的所有这些文件
    // 如果懒得清除，可以更改要创建的文件名
    for (i = 0; i < 3; i++) {
        // 创建目录
        printf("no %d:create dir %s\n", i, dir_path);
        err = xfile_mkdir(dir_path);
        if (err < 0) {
            if ((err == FS_ERR_EXISTED) && (i >= 1)) {
                // 只有在第一次创建时才会成功
                printf("dir exist %s, continue.\n", dir_path);
            } else {
                printf("create dir failed %s\n", dir_path);
                return err;
            }
        }
        
        for (j = 0; j < 50; j++) {
            // 创建文件
            sprintf(path, "%s/b%d.txt", dir_path, j);
            printf("no %d:create file %s\n", i, path);

            err = xfile_mkfile(path);
            if (err < 0) {
                if ((err == FS_ERR_EXISTED) && (i >= 1)) {
                    // 只有在第一次创建时才会成功
                    printf("file exist %s, continue.\n", path);
                } else {
                    printf("create file failed %s\n", path);
                    return err;
                }
            }

            // 进行一些读写测试,  写有点bug，写3遍就会丢数据
            err = file_write_test(path, 1024, 1, 1);
            if (err < 0) {
                printf("write file failed! %s\n", path);
                return err;
            }
            printf("create %s ok!\n", path);
        }
    }

    err = xfile_rmdir(dir_path);
    if (err != FS_ERR_NOT_EMPTY) {
        printf("rm dir failed!\n");
        return -1;
    }

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 50; j++) {
            // 创建文件
            sprintf(path, "%s/b%d.txt", dir_path, j);
            printf("no %d:rm file %s\n", i, path);

            err = xfile_rmfile(path);
            if (err < 0) {
                if ((err == FS_ERR_NONE) && (i >= 1)) {
                    // 只有在第一次创建时才会成功
                    printf("file not exist %s, continue.\n", path);
                } else {
                    printf("rm file failed %s\n", path);
                    return err;
                }
            }
        }
    }

    err = xfile_rmdir(dir_path);
    if (err != FS_ERR_OK) {
        printf("rm dir failed!\n");
        return -1;
    }

    printf("create test ok\n");
    return FS_ERR_OK;
}

xfat_err_t fs_resize_test (void) {
    xfile_t file;
    xfat_err_t err;
    u32_t i, offset = 0x2000;
    xfile_size_t file_size;

    const char * path = "/mp0/resize/file.txt";

    err = xfile_mkfile(path);
    if ((err < 0) && (err != FS_ERR_EXISTED)) {
        printf("create file failed!\n");
        return err;
    }

    err = xfile_open(&file, path);
    if (err < 0) {
        printf("open file failed!\n");
        return err;
    }

    // 前32字节内容是随机的
    err = xfile_resize(&file, 32);
    if (err < 0) {
        printf("resize file failed!\n");
        return err;
    }

    err = xfile_seek(&file, 32, SEEK_SET);
    if (err < 0) {
        printf("seek file failed!\n");
        return err;
    }

    if (xfile_write(write_buffer, sizeof(write_buffer), 1, &file) == 0) {
        printf("write file failed!\n");
        return err;
    }

    // 缩小文件 
    err = xfile_resize(&file, offset);
    if (err < 0) {
        printf("resize file failed!\n");
        return err;
    }

    err = xfile_seek(&file, offset, SEEK_SET);
    if (err < 0) {
        printf("seek file failed!\n");
        return err;
    }

    if (xfile_write(write_buffer, sizeof(write_buffer), 1, &file) == 0) {
        printf("write file failed!\n");
        return err;
    }

    xfile_size(&file, &file_size);
    if (file_size != (offset + sizeof(write_buffer))) {
        printf("resize test failed!\n");
        return err;
    }

    printf("resize test ok\n");
    return err;
}

xfat_err_t fs_format_test(void) {
    xdisk_part_t fmt_part;
    xfat_err_t err;
    xfat_fmt_ctrl_t ctrl;

    // 根据实际情况填写格式化哪个分区
    // 用序号格式化可能差了些
    err = xdisk_get_part(&disk, &fmt_part, 1);
    if (err < 0) {
        return err;
    }

    xfat_fmt_ctrl_init(&ctrl);
    ctrl.vol_name = "XFAT DISK";

    err = xfat_format(&fmt_part, &ctrl);
    if (err < 0) {
        return err;
    }

    printf("format test ok!\n");
    return err;
}

xfat_err_t fs_cluster_load_test(void) {
    xfat_err_t err;
    xfile_t file;
    const char * file_path = "/mp0/clusters/WinHex64.exe";

    err = xfile_open(&file, file_path);
    if (err < 0) {
        return err;
    }

    return err;
}

extern xdisk_driver_t vdisk_driver;

int main (void) {
    static u8_t disk_buf[DISK_BUF_SIZE(512, 1)];
    xfat_err_t err;
    int i;

    for (i = 0; i < sizeof(write_buffer) / sizeof(u32_t); i++) {
        write_buffer[i] = i;
    }

    err = disk_io_test();
    if (err) return err;

    err = xfat_init();
    if (err < 0) {
        printf("fat init failed!\n");
        return -1;
    }

    err = xdisk_open(&disk, "vidsk", &vdisk_driver, (void *)disk_path, disk_buf, sizeof(disk_buf));
    if (err) {
        printf("open disk failed!\n");
        return -1;
    }

    err = disk_part_test();
    if (err) return err;

    err = xdisk_get_part(&disk, &disk_part, 1);
    if (err < 0) {
        printf("read partition info failed!\n");
        return -1;
    }

    err = xfat_mount(&xfat, &disk_part, "mp0");
    if (err == -1) {
        printf("fat init failed!\n");
        return -1;
    }

    //err = fat_dir_test();
    //if (err) return err;

    //err = fat_file_test();
    //if (err) return err;

    //err = fs_open_test();
    //if (err) return err;

    //err = dir_trans_test();
    //if (err) return err;

    //err = fs_read_test();
    //if (err) return err;

    //err = fs_seek_test();
    //if (err) return err;

    //err = fs_modify_file_test();
    //if (err) return err;

    //err = fs_write_test();
    //if (err) return err;

    //err = fs_create_test();
    //if (err) return err;

    //err = fs_resize_test();
    //if (err) return err;

//    err = fs_format_test();
//    if (err) return err;

    err = fs_cluster_load_test();
    if (err) return err;

    err = xdisk_close(&disk);
    if (err) {
        printf("disk close failed!\n");
        return -1;
    }

    printf("Test End!\n");
    return 0;
}
