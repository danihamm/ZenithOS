MAKEFLAGS += -rR
.SUFFIXES:

TOOLCHAIN_PREFIX := $(shell cd ../../.. && pwd)/toolchain/local/bin/x86_64-elf-
ifneq ($(wildcard $(TOOLCHAIN_PREFIX)gcc),)
    CC := $(TOOLCHAIN_PREFIX)gcc
    AR := $(TOOLCHAIN_PREFIX)ar
else
    CC := gcc
    AR := ar
endif

SRCDIR   := src
OBJDIR   := obj
LIBC_DIR := ../../lib/libc
BINDIR   := ../../bin/os
LIBDIR   := ../../bin/lib/lua

CFLAGS := \
    -std=gnu11 \
    -g -O2 -pipe \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -Wno-unused-function \
    -Wno-sign-compare \
    -Wno-misleading-indentation \
    -Wno-implicit-fallthrough \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -m64 \
    -march=x86-64 \
    -msse \
    -msse2 \
    -mno-red-zone \
    -mcmodel=small \
    -D__MONTAUKOS__=1 \
    -DLUA_COMPAT_5_3 \
    -I$(SRCDIR) \
    -isystem ../../include/libc \
    -isystem $(shell $(CC) -print-file-name=include)

LDFLAGS := \
    -nostdlib \
    -static \
    -Wl,--build-id=none \
    -Wl,--gc-sections \
    -Wl,-m,elf_x86_64 \
    -z max-page-size=0x1000 \
    -T ../../link.ld

CORE_SRCS := \
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c \
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c \
    ltm.c lundump.c lvm.c lzio.c

LIB_SRCS := \
    lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c \
    loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c

BASE_SRCS := $(CORE_SRCS) $(LIB_SRCS)
BASE_OBJS := $(addprefix $(OBJDIR)/,$(BASE_SRCS:.c=.o))
LUA_MAIN  := $(OBJDIR)/lua.o

TARGET_LIB := $(LIBDIR)/liblua.a
TARGET_BIN := $(BINDIR)/lua.elf
STAGE_HEADERS := \
    $(LIBDIR)/include/lua.h \
    $(LIBDIR)/include/lauxlib.h \
    $(LIBDIR)/include/luaconf.h \
    $(LIBDIR)/include/lualib.h \
    $(LIBDIR)/include/lua.hpp

.PHONY: all clean

all: $(TARGET_LIB) $(TARGET_BIN) $(STAGE_HEADERS)

$(TARGET_LIB): $(BASE_OBJS) $(LIBC_DIR)/liblibc.a
	@mkdir -p $(LIBDIR)
	$(AR) rcs $@ $(BASE_OBJS)

$(TARGET_BIN): $(LUA_MAIN) $(TARGET_LIB) $(LIBC_DIR)/liblibc.a $(LIBC_DIR)/crt1.o $(LIBC_DIR)/crti.o $(LIBC_DIR)/crtn.o
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(LIBC_DIR)/crt1.o $(LIBC_DIR)/crti.o \
		$(LUA_MAIN) $(TARGET_LIB) $(LIBC_DIR)/liblibc.a $(LIBC_DIR)/crtn.o \
		-lgcc -o $@

$(LIBDIR)/include/%: $(SRCDIR)/%
	@mkdir -p $(LIBDIR)/include
	cp $< $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET_BIN) $(TARGET_LIB) $(LIBDIR)/include
