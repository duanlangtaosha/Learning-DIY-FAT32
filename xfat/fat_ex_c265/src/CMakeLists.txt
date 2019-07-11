cmake_minimum_required(VERSION 3.13)
project(untitled)

set(CMAKE_CXX_STANDARD 14)

add_executable(untitled xdisk.c fatfs_test.c xfat.h xfat.c driver.c)
