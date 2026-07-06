# Makefile — EXT2 File System Simulator
# Phase 3: 模块化构建（7 个源文件，.o 输出到 obj/）

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c99 -I./include
TARGET   = output/ext2fs
OBJDIR   = obj

SRCS     = src/main.c src/shell.c src/context.c \
           src/disk_io.c src/bitmap.c src/directory.c src/file_ops.c
OBJS     = $(SRCS:src/%.c=$(OBJDIR)/%.o)

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p output
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  BUILD   $@"

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS += -g -O0
debug: all

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "  CLEAN"

distclean: clean
	rm -f Ext2
	@echo "  DISTCLEAN (removed Ext2 disk image)"
