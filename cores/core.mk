# Builds a libretro core as a static library, using the source list from its Makefile.common.
# make -f cores/core.mk EMU_CORE=snes9x2002|gpsp|gambatte ARCH=pc|arm  -> out/<ARCH>/lib<EMU_CORE>.a
# (picodrive has its own recipe: cores/picodrive.mk)
ARCH ?= arm
EMU_CORE ?= gambatte

ifeq ($(ARCH),arm)
CROSS := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-
CC := $(CROSS)gcc
CXX := $(CROSS)g++
AR := $(CROSS)ar
ARCHF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -marm
else
CC := cc
CXX := c++
AR := ar
ARCHF :=
endif

# ---------- SNES: snes9x2002 (on ARM it uses the assembly CPU and SPC700) ----------
ifeq ($(EMU_CORE),snes9x2002)
CORE_DIR := cores/snes9x2002/src
LIBRETRO_DIR := cores/snes9x2002/libretro
ifeq ($(ARCH),arm)
ARM_ASM := 1
endif
include cores/snes9x2002/Makefile.common
SRC_C := $(SOURCES)
SRC_CXX :=
SRC_S := $(SOURCES_ASM)
FLAGS := $(DEFINES) $(INCLUDES) -I$(CORE_DIR) -DFRAMESKIP -DLSB_FIRST
endif

# ---------- GBA: gpsp (ARM dynamic recompiler; paged ROM to fit in the 55 MB of the stick) ----------
ifeq ($(EMU_CORE),gpsp)
CORE_DIR := cores/gpsp
ifeq ($(ARCH),arm)
HAVE_DYNAREC := 1
CPU_ARCH := arm
endif
include cores/gpsp/Makefile.common
SRC_C := $(SOURCES_C)
SRC_CXX := $(SOURCES_CC)
SRC_S := $(SOURCES_ASM)
FLAGS := $(INCFLAGS) -DHAVE_STRINGS_H -DHAVE_STDINT_H -DHAVE_INTTYPES_H -D__LIBRETRO__ -DINLINE=inline
ifeq ($(ARCH),arm)
FLAGS += -DHAVE_DYNAREC -DARM_ARCH -DMMAP_JIT_CACHE -DROM_BUFFER_SIZE=8 -DSMALL_TRANSLATION_CACHE
endif
endif

# ---------- Game Boy / Color: gambatte ----------
ifeq ($(EMU_CORE),gambatte)
CORE_DIR := cores/gambatte/libgambatte/src
include cores/gambatte/Makefile.common
SRC_C := $(SOURCES_C)
SRC_CXX := $(SOURCES_CXX)
SRC_S :=
FLAGS := $(INCFLAGS) -DHAVE_STDINT_H -DHAVE_INTTYPES_H -D__LIBRETRO__ -DVIDEO_RGB565 -DINLINE=inline
endif

OUT := out/$(ARCH)/$(EMU_CORE)
# normalizes paths with ../ (gambatte uses them) so the pattern rule matches
SOURCES_ALL := $(patsubst $(CURDIR)/%,%,$(abspath $(SRC_C) $(SRC_CXX) $(SRC_S)))
OBJS := $(patsubst cores/%,$(OUT)/%.o,$(SOURCES_ALL))
CFLAGS := -O3 -fomit-frame-pointer -fno-strict-aliasing -w $(ARCHF) $(FLAGS) -DNDEBUG
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions

out/$(ARCH)/lib$(EMU_CORE).a: $(OBJS)
	$(AR) rcs $@ $^

$(OUT)/%.c.o: cores/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/%.cpp.o: cores/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.cc.o: cores/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OUT)/%.S.o: cores/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/%.s.o: cores/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
