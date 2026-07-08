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

# Clean build artifacts (.o, .ko, .mod, etc.)
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

# Deep clean: remove ALL generated files including disk images
distclean: clean
	@echo "Cleaning all generated files..."
	@rm -f *.o *.ko *.mod *.mod.c *.mod.o modules.order Module.symvers
	@rm -f .*.cmd .*.o .module-common.o
	@rm -f src/*.o src/.*.cmd
	@rm -f test.img Ext2
	@echo "distclean complete."

.PHONY: all clean distclean
