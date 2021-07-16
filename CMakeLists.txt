# Copyright (c) 2020 Alexander Kozhinov
# Mail: AlexanderKozhinov@yandex.com
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(BOARD stm32h750b_poemodule)

add_definitions(-DUSE_STACK_SIZE=16384)

include_directories(
	"include"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(civetweb_http_server)

target_sources(app PRIVATE
	src/main.c
	src/libc_extensions.c
)
