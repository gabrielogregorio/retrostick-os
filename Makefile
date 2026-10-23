# make pc    -> out/menu-pc (window on the PC)
# make arm   -> out/menu-arm (static binary for the stick: ARMv6 + VFP, musl)
# make run   -> opens the preview on the PC
# make test  -> runs the ARM binary under qemu-arm, headless, and saves out/stick-shot.png
CROSS := toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-
SRC := src/main.c src/gfx.c src/aero.c src/icons.c src/music.c src/plat_exec.c
CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

all: pc arm

out/menu-pc: $(SRC) src/plat_sdl.c src/*.h
	cc $(CFLAGS) $(SRC) src/plat_sdl.c -o $@ $$(sdl2-config --cflags --libs) -lm

out/menu-arm: $(SRC) src/plat_stick.c src/*.h
	$(CROSS)gcc $(CFLAGS) -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -static -no-pie -s \
		$(SRC) src/plat_stick.c -o $@ -lm

ARMF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp
LRINC := -Icores/fceumm/src/drivers/libretro/libretro-common/include

# NES = QuickNES (light). FCEUmm stays as nes-fceumm-*, more accurate and heavier
# (on the stick its emulation does not fit in the 16.6 ms of a frame at 60 Hz).
out/nes-pc: src/runner.c src/plat_sdl.c src/*.h out/pc/libquicknes.a
	c++ -x c $(CFLAGS) $(LRINC) -c src/runner.c -o out/pc/runner.o
	c++ -x c $(CFLAGS) $$(sdl2-config --cflags) -c src/plat_sdl.c -o out/pc/plat_sdl.o
	c++ out/pc/runner.o out/pc/plat_sdl.o out/pc/libquicknes.a -o $@ $$(sdl2-config --libs) -lm

out/nes-arm: src/runner.c src/plat_stick.c src/*.h out/arm/libquicknes.a
	$(CROSS)g++ -x c $(CFLAGS) $(LRINC) $(ARMF) -c src/runner.c -o out/arm/runner.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/plat_stick.c -o out/arm/plat_stick.o
	$(CROSS)g++ $(ARMF) -static -no-pie -s out/arm/runner.o out/arm/plat_stick.o out/arm/libquicknes.a -o $@ -lm

out/nes-fceumm-arm: src/runner.c src/plat_stick.c src/*.h out/arm/libfceumm.a
	$(CROSS)gcc $(CFLAGS) $(LRINC) $(ARMF) -static -no-pie -s \
		src/runner.c src/plat_stick.c out/arm/libfceumm.a -o $@ -lm

out/pc/libquicknes.a:
	$(MAKE) -f cores/quicknes.mk ARCH=pc -j8
out/arm/libquicknes.a:
	$(MAKE) -f cores/quicknes.mk ARCH=arm -j8

out/pc/libfceumm.a:
	$(MAKE) -f cores/fceumm.mk ARCH=pc -j8
out/arm/libfceumm.a:
	$(MAKE) -f cores/fceumm.mk ARCH=arm -j8

# Other systems: the same runner (runner.c) linked to the core of each one.
EMU_snes := snes9x2002
EMU_md := picodrive
EMU_sms := picodrive
EMU_gba := gpsp
EMU_gb := gambatte
EMU_gg := picodrive
EMU_a26 := stella2014
EMU_ngp := race
EMU_ws := wswan
SYSTEMS := snes md sms gba gb gg a26 ngp ws

.SECONDEXPANSION:
out/%-arm: src/runner.c src/plat_stick.c src/*.h out/arm/lib$$(EMU_$$*).a
	$(CROSS)g++ -x c $(CFLAGS) $(LRINC) $(ARMF) -c src/runner.c -o out/arm/runner.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/plat_stick.c -o out/arm/plat_stick.o
	$(CROSS)g++ $(ARMF) -static -no-pie -s out/arm/runner.o out/arm/plat_stick.o out/arm/lib$(EMU_$*).a -o $@ -lm

out/%-pc: src/runner.c src/plat_sdl.c src/*.h out/pc/lib$$(EMU_$$*).a
	c++ -x c $(CFLAGS) $(LRINC) -c src/runner.c -o out/pc/runner.o
	c++ -x c $(CFLAGS) $$(sdl2-config --cflags) -c src/plat_sdl.c -o out/pc/plat_sdl.o
	c++ out/pc/runner.o out/pc/plat_sdl.o out/pc/lib$(EMU_$*).a -o $@ $$(sdl2-config --libs) -lm -lz

out/arm/libpicodrive.a:
	$(MAKE) -f cores/picodrive.mk ARCH=arm
out/pc/libpicodrive.a:
	$(MAKE) -f cores/picodrive.mk ARCH=pc
out/arm/lib%.a:
	$(MAKE) -f cores/core.mk EMU_CORE=$* ARCH=arm -j8
out/pc/lib%.a:
	$(MAKE) -f cores/core.mk EMU_CORE=$* ARCH=pc -j8

pc: out/menu-pc out/nes-pc $(SYSTEMS:%=out/%-pc)
arm: out/menu-arm out/nes-arm $(SYSTEMS:%=out/%-arm)

run: pc
	./out/menu-pc

test: arm
	STICK_ROMS=roms STICK_SERIAL=/dev/null STICK_KEYS=R,D,D,A STICK_SHOT=out/stick-shot.ppm \
		qemu-arm-static ./out/menu-arm
	python3 -c "from PIL import Image; Image.open('out/stick-shot.ppm').save('out/stick-shot.png')"

.PHONY: all pc arm run test

# make card -> out/card/retro ready to copy to the root of the SD card (FAT32 with a partition)
card: arm
	mkdir -p out/card && cp -r card-base/. out/card/ && cp out/menu-arm out/nes-arm out/card/retro/ && mkdir -p out/card/retro/saves
	@echo "copy out/card/retro to the root of the SD card"

.PHONY: card
