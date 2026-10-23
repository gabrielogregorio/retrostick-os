# Builds the QuickNES core (NES, much lighter than FCEUmm) as a static library.
# make -f cores/quicknes.mk ARCH=pc|arm  -> out/<ARCH>/libquicknes.a
ARCH ?= arm
CORE_DIR := cores/quicknes
include cores/quicknes/Makefile.common

ifeq ($(ARCH),arm)
CXX := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-g++
AR := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-ar
ARCHF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp
else
CXX := c++
AR := ar
ARCHF :=
endif
OUT := out/$(ARCH)/quicknes
OBJS := $(patsubst cores/%.cpp,$(OUT)/%.o,$(SOURCES_CXX))
CXXFLAGS := -O3 -fomit-frame-pointer -fno-strict-aliasing -fno-rtti -w $(ARCHF) $(INCFLAGS) -D__LIBRETRO__ -DNDEBUG

out/$(ARCH)/libquicknes.a: $(OBJS)
	$(AR) rcs $@ $^

$(OUT)/%.o: cores/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
