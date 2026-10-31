# picodrive (Mega Drive + Master System) as a static library. Uses its own Makefile.libretro.
# make -f cores/picodrive.mk ARCH=arm|pc  -> out/<ARCH>/libpicodrive.a
# ARM: assembly CPU cores (Cyclone 68000, DrZ80, drawing/audio in asm). Cyclone.s is generated FIRST,
# with the PC compiler (the generator has to run here, and a command-line CC would take over).
# PC: builds in a copy (out/pc/src-picodrive) so objects do not mix with the ARM build.
ARCH ?= arm
# Our ARCH is an arm|pc switch, but Makefile.libretro has a CPU variable with the same name: keep ours
# off the sub-make command line and environment, or it would override what that Makefile sets or detects.
MAKEOVERRIDES := $(filter-out ARCH=%,$(MAKEOVERRIDES))
unexport ARCH
P := cores/picodrive
T := $(CURDIR)/toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-
F := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp

ifeq ($(ARCH),arm)
out/arm/libpicodrive.a:
	$(MAKE) -C $(P)/cpu/cyclone CONFIG_FILE=../cyclone_config.h HAVE_ARMv6=1
	$(MAKE) -C $(P) -f Makefile.libretro platform=armv6-softfloat-armasm STATIC_LINKING_LINK=1 \
		TARGET=picodrive_libretro.a HAVE_ARMv6=1 CC="$(T)gcc $(F)" CXX="$(T)g++ $(F)" AR=$(T)ar \
		AS="$(T)as -mcpu=arm1176jzf-s -mfpu=vfp" -j8
	@mkdir -p out/arm
	cp $(P)/picodrive_libretro.a $@
else
out/pc/libpicodrive.a:
	@mkdir -p out/pc
	[ -d out/pc/src-picodrive ] || cp -r $(P) out/pc/src-picodrive
	$(MAKE) -C out/pc/src-picodrive -f Makefile.libretro platform=unix clean
	@# their clean does not remove the .a (and ar would append onto the old one): move it away
	@[ ! -f out/pc/src-picodrive/picodrive_libretro.a ] || mv out/pc/src-picodrive/picodrive_libretro.a out/pc/picodrive_libretro.a.old
	$(MAKE) -C out/pc/src-picodrive -f Makefile.libretro platform=unix STATIC_LINKING_LINK=1 \
		TARGET=picodrive_libretro.a -j8
	cp out/pc/src-picodrive/picodrive_libretro.a $@
endif
