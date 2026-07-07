# SPDX-License-Identifier: GPL-2.0
#
# Kbuild Makefile for ext2_sim kernel module
#

obj-m += ext2_sim.o
ext2_sim-objs := src/super.o src/inode.o src/file.o src/balloc.o src/dir.o

ccflags-y := -I$(src)/include -Wall -Wno-unused-function

# Default target: build the kernel module
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# Clean build artifacts
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

.PHONY: all clean
