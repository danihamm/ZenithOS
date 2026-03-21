# Makefile for TCC on MontaukOS
# Copyright (c) 2026 Daniel Hammer

MAKEFLAGS += -rR
.SUFFIXES:

# ---- Toolchain ----

TOOLCHAIN_PREFIX := $(shell cd ../../.. && pwd)/toolchain/local/bin/x86_64-elf-
ifneq ($(wildcard $(TOOLCHAIN_PREFIX)gcc),)
    CC  := $(TOOLCHAIN_PREFIX)gcc
    CXX := $(TOOLCHAIN_PREFIX)g++
    AR  := $(TOOLCHAIN_PREFIX)ar
else
    CC  := gcc
    CXX := g++
    AR  := ar
endif

# ---- Paths ----

PROG_INC := ../../include
LIBDIR   := ../../lib
OBJDIR   := obj
BINDIR   := ../../bin/os

# ---- Compiler flags ----

CFLAGS := \
    -std=gnu11 \
    -g -O2 -pipe \
    -Wall \
    -Wno-unused-parameter \
    -Wno-unused-function \
    -Wno-sign-compare \
    -Wno-misleading-indentation \
    -Wno-implicit-fallthrough \
    -Wno-stringop-truncation \
    -Wno-format-truncation \
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
    -fno-tree-loop-distribute-patterns \
    -D__MONTAUKOS__=1 \
    -DONE_SOURCE=0 \
    -DTCC_TARGET_X86_64=1 \
    -DCONFIG_TCC_STATIC=1 \
    -I. \
    -isystem $(PROG_INC)/libc \
    -isystem $(shell $(CC) -print-file-name=include)

LDFLAGS := \
    -nostdlib \
    -static \
    -Wl,--build-id=none \
    -Wl,--gc-sections \
    -Wl,-m,elf_x86_64 \
    -z max-page-size=0x1000 \
    -T ../../link.ld

# ---- Sources ----

TCC_SRCS := \
    libtcc.c \
    tccpp.c \
    tccgen.c \
    tccelf.c \
    tccasm.c \
    tccdbg.c \
    x86_64-gen.c \
    x86_64-link.c \
    i386-asm.c \
    tcc.c

COMPAT_SRCS := \
    montauk_main.c

ASM_SRCS := \
    setjmp.S

TCC_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(TCC_SRCS))
COMPAT_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(COMPAT_SRCS))
ASM_OBJS := $(patsubst %.S,$(OBJDIR)/%.o,$(ASM_SRCS))

ALL_OBJS := $(TCC_OBJS) $(COMPAT_OBJS) $(ASM_OBJS)

TARGET := $(BINDIR)/tcc.elf

# ---- CRT objects (compiled for linking by TCC) ----

CRT_CFLAGS := \
    -std=gnu11 \
    -O2 \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-PIC \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -fno-tree-loop-distribute-patterns \
    -isystem $(PROG_INC)/libc \
    -isystem $(shell $(CC) -print-file-name=include)

CRT_DIR := ../../bin/lib/tcc/lib
CRT_OBJS := $(CRT_DIR)/crt1.o $(CRT_DIR)/crti.o $(CRT_DIR)/crtn.o

# ---- Rules ----

.PHONY: all clean

all: $(TARGET) $(CRT_OBJS) $(CRT_DIR)/libc.a

$(TARGET): $(ALL_OBJS) $(LIBDIR)/libc/liblibc.a
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(ALL_OBJS) $(LIBDIR)/libc/liblibc.a -lgcc -o $@

$(OBJDIR)/%.o: %.c Makefile config.h montauk_compat.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.S Makefile
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CRT_DIR)/crt1.o: crt/crt1.c
	@mkdir -p $(CRT_DIR)
	$(CC) $(CRT_CFLAGS) -c $< -o $@

$(CRT_DIR)/crti.o: crt/crti.c
	@mkdir -p $(CRT_DIR)
	$(CC) $(CRT_CFLAGS) -c $< -o $@

$(CRT_DIR)/crtn.o: crt/crtn.c
	@mkdir -p $(CRT_DIR)
	$(CC) $(CRT_CFLAGS) -c $< -o $@

$(CRT_DIR)/libc.a: $(LIBDIR)/libc/liblibc.a
	@mkdir -p $(CRT_DIR)
	cp $< $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
