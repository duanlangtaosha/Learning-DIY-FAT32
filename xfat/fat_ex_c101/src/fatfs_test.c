/**
 * 本源码配套的课程为 - 从0到1动手写FAT32文件系统。每个例程对应一个课时，尽可能注释。
 * 作者：李述铜
 * 课程网址：http://01ketang.cc
 * 版权声明：本源码非开源，二次开发，或其它商用前请联系作者。
 */
#include <stdio.h>
#include "xdisk.h"
#include "xfat.h"

static int array[32];

void print_int (int v) {
    printf("v = %d\n", v);
}

int main(void) {
    int * p_array = array;

    for (int i = 0; i < 100; i++) {
        print_int(i);
    }

    p_array[0] = 32;

    printf("Test End!\n");
    return 0;
}