# Makefile for rpgdemo (Montauk Quest - 2D RPG demo) on MontaukOS
# Copyright (c) 2026 Daniel Hammer

MAKEFLAGS += -rR
.SUFFIXES:

# ---- Toolchain ----

TOOLCHAIN_PREFIX := $(shell cd ../../.. && pwd)/toolchain/local/bin/x86_64-elf-
ifneq ($(wildcard $(TOOLCHAIN_PREFIX)gcc),)
    CXX := $(TOOLCHAIN_PREFIX)g++
else
    CXX := g++
endif

# ---- Paths ----

PROG_INC := ../../include
LINK_LD  := ../../link.ld
BINDIR   := ../../bin
OBJDIR   := obj
LIBDIR   := ../../lib

# Game asset source directory
ASSET_SRC := ../../gameengine/Cute_Fantasy_Free

# ---- Compiler flags ----

CXXFLAGS := \
    -std=gnu++20 \
    -g -O2 -pipe \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -Wno-unused-function \
    -Wno-missing-field-initializers \
    -nostdinc \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-PIC \
    -fno-rtti \
    -fno-exceptions \
    -ffunction-sections \
    -fdata-sections \
    -fno-tree-loop-distribute-patterns \
    -m64 \
    -march=x86-64 \
    -msse \
    -msse2 \
    -mno-red-zone \
    -mcmodel=small \
    -I $(PROG_INC) \
    -I . \
    -isystem $(PROG_INC)/libc \
    -isystem ../../../kernel/freestnd-c-hdrs/x86_64/include \
    -isystem ../../../kernel/freestnd-cxx-hdrs/x86_64/include

# ---- Linker flags ----

LDFLAGS := \
    -nostdlib \
    -static \
    -Wl,--build-id=none \
    -Wl,--gc-sections \
    -Wl,-m,elf_x86_64 \
    -z max-page-size=0x1000 \
    -T $(LINK_LD)

# ---- Source files ----

SRCS := main.cpp stb_impl.cpp
OBJS := $(addprefix $(OBJDIR)/,$(SRCS:.cpp=.o))

# ---- Target ----

TARGET := $(BINDIR)/apps/rpgdemo/rpgdemo.elf
APP_DIR := $(BINDIR)/apps/rpgdemo

.PHONY: all clean assets

all: $(TARGET) assets

$(TARGET): $(OBJS) $(LINK_LD) Makefile
	mkdir -p $(APP_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBDIR)/libc/liblibc.a -o $@

$(OBJDIR)/%.o: %.cpp Makefile
	mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Copy game assets into the app bundle
assets:
	mkdir -p $(APP_DIR)
	cp $(ASSET_SRC)/Player/Player.png $(APP_DIR)/Player.png
	cp $(ASSET_SRC)/Player/Player_Actions.png $(APP_DIR)/Player_Actions.png
	cp $(ASSET_SRC)/Enemies/Skeleton.png $(APP_DIR)/Skeleton.png
	cp $(ASSET_SRC)/Enemies/Slime_Green.png $(APP_DIR)/Slime_Green.png
	cp $(ASSET_SRC)/Tiles/Grass_Middle.png $(APP_DIR)/Grass_Middle.png
	cp $(ASSET_SRC)/Tiles/Path_Middle.png $(APP_DIR)/Path_Middle.png
	cp $(ASSET_SRC)/Tiles/Water_Middle.png $(APP_DIR)/Water_Middle.png
	cp $(ASSET_SRC)/Tiles/Cliff_Tile.png $(APP_DIR)/Cliff_Tile.png
	cp "$(ASSET_SRC)/Outdoor decoration/Oak_Tree.png" $(APP_DIR)/Oak_Tree.png
	cp "$(ASSET_SRC)/Outdoor decoration/Oak_Tree_Small.png" $(APP_DIR)/Oak_Tree_Small.png
	cp "$(ASSET_SRC)/Outdoor decoration/House_1_Wood_Base_Blue.png" $(APP_DIR)/House.png
	cp "$(ASSET_SRC)/Outdoor decoration/Chest.png" $(APP_DIR)/Chest.png
	cp "$(ASSET_SRC)/Outdoor decoration/Fences.png" $(APP_DIR)/Fences.png
	cp "$(ASSET_SRC)/Outdoor decoration/Bridge_Wood.png" $(APP_DIR)/Bridge_Wood.png
	cp "$(ASSET_SRC)/Outdoor decoration/Outdoor_Decor_Free.png" $(APP_DIR)/Outdoor_Decor.png
	cp $(ASSET_SRC)/Animals/Chicken/Chicken.png $(APP_DIR)/Chicken.png
	cp $(ASSET_SRC)/Animals/Cow/Cow.png $(APP_DIR)/Cow.png
	cp $(ASSET_SRC)/Animals/Pig/Pig.png $(APP_DIR)/Pig.png
	cp $(ASSET_SRC)/Animals/Sheep/Sheep.png $(APP_DIR)/Sheep.png

clean:
	rm -rf $(OBJDIR) $(TARGET)
