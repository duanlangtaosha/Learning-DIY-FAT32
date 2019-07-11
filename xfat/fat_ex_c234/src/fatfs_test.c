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

    err = xfile_close(&top_dir);
    if (err < 0) {
        printf("close file failed!\n");
        return -1;
    }

    printf("file trans test ok\n");
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

    err = dir_trans_test();
    if (err) return err;

    err = xdisk_close(&disk);
    if (err) {
        printf("disk close failed!\n");
        return -1;
    }

    printf("Test End!\n");
    return 0;
}
