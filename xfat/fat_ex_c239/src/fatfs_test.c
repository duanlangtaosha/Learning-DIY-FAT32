/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xdisk.h"
#include "xfat.h"

extern xdisk_driver_t vdisk_driver;

const char * disk_path_test = "disk_test.img";
const char * disk_path = "disk.img";

static u32_t write_buffer[160*1024];
static u32_t read_buffer[160*1024];

xdisk_t disk;
xdisk_part_t disk_part;
xfat_t xfat;

// io测试，测试通过要注意关掉
int disk_io_test (void) {
    int err;
    xdisk_t disk_test;

    memset(read_buffer, 0, sizeof(read_buffer));

    err = xdisk_open(&disk_test, "vidsk_test", &vdisk_driver, (void *)disk_path_test);
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
    curr_cluster = 4565;    // 62.txt
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

            err = xfile_open_sub(file, fileinfo.file_name, &sub_file);
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
    err = xfile_open(&xfat, &top_dir, "/read/..");
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

    printf("\ntry to list all sub files!\n");

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

int file_read_and_check(const char * path, xfile_size_t elem_size,  xfile_size_t e_count) {
    xfile_t file;
    xfile_size_t readed_count;
    xfile_size_t curr_offset = 0;

    xfat_err_t err = xfile_open(&xfat, &file, path);
    if (err != FS_ERR_OK) {
        printf("open file failed! %s\n", path);
        return -1;
    }

    if ((readed_count = xfile_read(read_buffer, elem_size, e_count, &file)) > 0) {
        u32_t i = 0;
        u32_t num_start = (u32_t)curr_offset / 4;       // 起始数值
        xfile_size_t bytes_count = readed_count * elem_size;    // 总的字节数
        for (i = 0; i < bytes_count; i += 4) {
            if (read_buffer[i / 4] != num_start++) {
                printf("read file failed!\n");
                return -1;
            }
        }
    }

    if (xfile_error(&file) < 0) {
        printf("read failed!\n");
        return -1;
    }

    xfile_close(&file);

    return FS_ERR_OK;
}

int fs_read_test (void) {
    const char * file_0b_path = "/read/0b.bin";
    const char * file_1MB_path = "/read/1MB.bin";   // 从0x00000000~0x0003FFFF的二进制文件
    xfat_err_t err;

    printf("\nfile read test!\n");

    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_0b_path, 32, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 不超过一个扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_1MB_path, disk.sector_size - 32, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 刚好一个扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_1MB_path, disk.sector_size, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 跨扇区的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_1MB_path, disk.sector_size + 14, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    // 刚好超过一个簇的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_1MB_path, xfat.cluster_byte_size + 32, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }
    
    // 跨多个簇的读取
    memset(read_buffer, 0, sizeof(read_buffer));
    err = file_read_and_check(file_1MB_path, 2 * xfat.cluster_byte_size + 32, 1);
    if (err < 0) {
        printf("read failed!");
        return -1;
    }

    printf("\nfile read test ok\n");
    return FS_ERR_OK;
}

int _fs_seek_test(xfile_t * file, xfile_orgin_t orgin, xfile_ssize_t offset) {
    int err = 0;
    xfile_ssize_t target_pos;
    u32_t count;

    switch (orgin) {
        case XFAT_SEEK_SET:
            target_pos = offset;
            break;
        case XFAT_SEEK_END:
            target_pos = file->size + offset;
            break;
        case XFAT_SEEK_CUR:
            target_pos = file->pos + offset;
            break;
        default:
            target_pos = 0;
            break;
    }

    err = xfile_seek(file, offset, orgin);
    if (err) {
        printf("seek error\n");
        return -1;
    }

    if (xfile_tell(file) != target_pos) {
        printf("seek error\n");
        return -1;
    }

    count = xfile_read(read_buffer, 1, 1, file);
    if (count < 1) {
        printf("seek error\n");
        return -1;
    }

    if (*(u8_t *)read_buffer != (target_pos % 256)){
        printf("seek error\n");
        return -1;
    }
    return 0;
}

int fs_seek_test(void) {
    xfat_err_t err;
    xfile_t file;

    printf("\nfile seek test!\n");

    err = xfile_open(&xfat, &file, "/seek/1MB.bin");
    if (err != FS_ERR_OK) {
        printf("open file failed!\n");
        return -1;
    }

    err = _fs_seek_test(&file, XFAT_SEEK_SET, 32);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_SET, 576);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_SET, 4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_SET, -1);
    if (err == FS_ERR_OK) return -1;

    err = _fs_seek_test(&file, XFAT_SEEK_CUR, 32);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, 576);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, 4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, -32);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, -512);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, -1024);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_CUR, -0xFFFFFFF);
    if (err == FS_ERR_OK) return -1;

    err = _fs_seek_test(&file, XFAT_SEEK_END, -32);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_END, -576);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_END, -4193);
    if (err) return err;
    err = _fs_seek_test(&file, XFAT_SEEK_END, 32);
    if (err == FS_ERR_OK) return -1;

    xfile_close(&file);
    printf("all seek test ok!\n");
    return 0;
}

int fs_open_test (void) {
    const char * not_exist_path = "/file_not_exist.txt";
    const char * exist_path = "/12345678ABC";    // 注意：文件名要大写
    const char * file1 = "/open/file.txt";
    const char * file2 = "/open/a0/a1/a2/a3/a4/a5/a6/a7/a8/a9/a10/a11/a12/a13/a14/a15/a16/a17/a18/a19/file.txt";
    xfat_err_t err;
    xfile_t file;

    printf("fs_open test...\n");

    err = xfile_open(&xfat, &file, "/");
    if (err) {
        printf("open file failed %s!\n", "/");
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&xfat, &file, not_exist_path);
    if (err == 0) {
        printf("open file ok %s!\n", not_exist_path);
        return -1;
    }

    err = xfile_open(&xfat, &file, exist_path);
    if (err < 0) {
        printf("open file failed %s!\n", exist_path);
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&xfat, &file, file1);
    if (err < 0) {
        printf("open file failed %s!\n", file1);
        return -1;
    }
    xfile_close(&file);

    err = xfile_open(&xfat, &file, file2);
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
    const char * dir_path = "/modify/a0/a1/a2/";
    const char file_name1[] = "ABC.efg";
    const char file_name2[] = "efg.ABC";
    const char * new_name;
    char curr_path[64];

    printf("modify file attr test...\n");

    printf("\n Before rename:\n");

    // 显示原目录下的文件
    err = xfile_open(&xfat, &file, dir_path);
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
    err = xfile_open(&xfat, &file, curr_path);
    if (err < 0) {
        // 打开文件1失败，则当前文件2存在，新名称为文件1名称
        sprintf(curr_path, "%s%s", dir_path, file_name2);
        new_name = file_name1;
    } else {
        sprintf(curr_path, "%s%s", dir_path, file_name1);
        new_name = file_name2;
    }

    // 文件重命名
    err = xfile_rename(&xfat, curr_path, new_name);
    if (err < 0) {
        printf("rename failed: %s -- to -- %s\n", curr_path, new_name);
        return err;
    }
    xfile_close(&file);

    printf("\n After rename:\n");

    // 重命名后，列表显示所有文件，显示命名状态
    err = xfile_open(&xfat, &file, dir_path);
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

int main (void) {
    xfat_err_t err;
    int i;

    for (i = 0; i < sizeof(write_buffer) / sizeof(u32_t); i++) {
        write_buffer[i] = i;
    }

//    err = disk_io_test();
//    if (err) return err;

    err = xdisk_open(&disk, "vidsk", &vdisk_driver, (void *)disk_path);
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

    err = xfat_open(&xfat, &disk_part);
    if (err < 0) {
        return err;
    }

//    err = fat_dir_test();
//    if (err) return err;

//    err = fat_file_test();
//    if (err) return err;

//    err = fs_open_test();
//    if (err) return err;

//    err = dir_trans_test();
//    if (err) return err;

//    err = fs_read_test();
//    if (err < 0) {
//        printf("read tesst failed");
//        return -1;
//    }

//    err = fs_seek_test();
//    if (err) return err;

    err = fs_modify_file_test();
    if (err) return err;

    err = xdisk_close(&disk);
    if (err) {
        printf("disk close failed!\n");
        return -1;
    }

    printf("Test End!\n");
    return 0;
}
