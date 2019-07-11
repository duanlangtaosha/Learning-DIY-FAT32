cmake_minimum_required(VERSION 3.13)
project(untitled)

set(CMAKE_CXX_STANDARD 14)

add_executable(untitled xdisk.c ../../fat_ex_c247/src/fatfs_test.c xfat.h ../../fat_ex_c247/src/xfat.c driver.c)
