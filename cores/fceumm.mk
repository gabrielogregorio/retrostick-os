# Builds the FCEUmm core (NES) as a static library.
# make -f cores/fceumm.mk ARCH=pc   -> out/pc/libfceumm.a
# make -f cores/fceumm.mk ARCH=arm  -> out/arm/libfceumm.a  (ARMv6 + VFP, musl)
ARCH ?= arm
CORE_DIR := cores/fceumm/src
HAVE_NTSC := 0
HAVE_HDPACK := 0
WANT_32BPP := 0
include cores/fceumm/Makefile.common

ifeq ($(ARCH),arm)
CC := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-gcc
AR := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-ar
ARCHF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp
else
CC := cc
AR := ar
ARCHF :=
endif
OUT := out/$(ARCH)/fceumm
OBJS := $(patsubst cores/%.c,$(OUT)/%.o,$(SOURCES_C))
CFLAGS := -O3 -fomit-frame-pointer -fno-strict-aliasing -w $(ARCHF) $(INCFLAGS) $(COREDEFINES) -DINLINE=inline -DHAVE_STDINT_H

out/$(ARCH)/libfceumm.a: $(OBJS)
	$(AR) rcs $@ $^

$(OUT)/%.o: cores/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
