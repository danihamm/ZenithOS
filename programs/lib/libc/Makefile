# Makefile for shared libc static library on MontaukOS
# Copyright (c) 2025-2026 Daniel Hammer

MAKEFLAGS += -rR
.SUFFIXES:

# ---- Toolchain ----

TOOLCHAIN_PREFIX := $(shell cd ../../.. && pwd)/toolchain/local/bin/x86_64-elf-
ifneq ($(wildcard $(TOOLCHAIN_PREFIX)gcc),)
    CC := $(TOOLCHAIN_PREFIX)gcc
    AR := $(TOOLCHAIN_PREFIX)ar
else
    CC := gcc
    AR := ar
endif

# ---- Paths ----

LIBC_INC := ../../include/libc
OBJDIR   := obj
CRTDIR   := crt
ASM_OBJS := $(OBJDIR)/setjmp.o

# ---- Compiler flags ----

CFLAGS := \
    -std=gnu11 \
    -g -O2 -pipe \
    -Wall \
    -Wno-unused-parameter \
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
    -isystem $(LIBC_INC) \
    -isystem $(shell $(CC) -print-file-name=include)

CRT_CFLAGS := \
    -std=gnu11 \
    -O2 \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-PIC \
    -m64 \
    -march=x86-64 \
    -mno-red-zone \
    -fno-tree-loop-distribute-patterns \
    -isystem $(LIBC_INC) \
    -isystem $(shell $(CC) -print-file-name=include)

# ---- Target ----

TARGET := liblibc.a
CRT_OBJS := crt1.o crti.o crtn.o

.PHONY: all clean

all: $(TARGET) $(CRT_OBJS)

$(TARGET): $(OBJDIR)/libc.o $(ASM_OBJS)
	$(AR) rcs $@ $^

$(OBJDIR)/libc.o: libc.c Makefile
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/setjmp.o: setjmp.S Makefile
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

crt1.o: $(CRTDIR)/crt1.c Makefile
	$(CC) $(CRT_CFLAGS) -c $< -o $@

crti.o: $(CRTDIR)/crti.c Makefile
	$(CC) $(CRT_CFLAGS) -c $< -o $@

crtn.o: $(CRTDIR)/crtn.c Makefile
	$(CC) $(CRT_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET) $(CRT_OBJS)
