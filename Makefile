# make pc    -> out/menu-pc (window on the PC)
# make arm   -> out/menu-arm (static binary for the stick: ARMv6 + VFP, musl)
# make run   -> opens the preview on the PC
# make smoke-arm -> runs the ARM menu under qemu-arm, headless, and saves out/stick-shot.png
# make test  -> unit and e2e tests (no emulator core, no ROM, no window)
# make demo  -> records docs/demo.gif from the menu on mock games
CROSS ?= toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-
SRC := src/main.c src/gfx.c src/aero.c src/icons.c src/music.c src/plat_exec.c
FONT := src/font_data.h
PY ?= python3
CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

all: pc arm

# Generated at build time from the system Noto Sans, not stored in the repo.
src/font_data.h: tools/gen_font.py
	$(PY) tools/gen_font.py

out/menu-pc: $(SRC) src/plat_sdl.c src/*.h src/font_data.h
	cc $(CFLAGS) $(SRC) src/plat_sdl.c -o $@ $$(sdl2-config --cflags --libs) -lm

out/menu-arm: $(SRC) src/plat_stick.c src/*.h src/font_data.h
	$(CROSS)gcc $(CFLAGS) -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -static -no-pie -s \
		$(SRC) src/plat_stick.c -o $@ -lm

ARMF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp
LRINC := -Icores/fceumm/src/drivers/libretro/libretro-common/include

# NES = QuickNES (light). FCEUmm stays as nes-fceumm-*, more accurate and heavier
# (on the stick its emulation does not fit in the 16.6 ms of a frame at 60 Hz).
out/nes-pc: src/runner.c src/plat_sdl.c src/*.h src/font_data.h out/pc/libquicknes.a
	c++ -x c $(CFLAGS) $(LRINC) -c src/runner.c -o out/pc/runner.o
	c++ -x c $(CFLAGS) $$(sdl2-config --cflags) -c src/plat_sdl.c -o out/pc/plat_sdl.o
	c++ -x c $(CFLAGS) -c src/pause.c -o out/pc/pause.o
	c++ -x c $(CFLAGS) -c src/gfx.c -o out/pc/gfx.o
	c++ out/pc/runner.o out/pc/plat_sdl.o out/pc/pause.o out/pc/gfx.o out/pc/libquicknes.a -o $@ $$(sdl2-config --libs) -lm

out/nes-arm: src/runner.c src/plat_stick.c src/*.h src/font_data.h out/arm/libquicknes.a
	$(CROSS)g++ -x c $(CFLAGS) $(LRINC) $(ARMF) -c src/runner.c -o out/arm/runner.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/plat_stick.c -o out/arm/plat_stick.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/pause.c -o out/arm/pause.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/gfx.c -o out/arm/gfx.o
	$(CROSS)g++ $(ARMF) -static -no-pie -s out/arm/runner.o out/arm/plat_stick.o out/arm/pause.o out/arm/gfx.o out/arm/libquicknes.a -o $@ -lm

out/nes-fceumm-arm: src/runner.c src/plat_stick.c src/*.h src/font_data.h out/arm/libfceumm.a
	$(CROSS)gcc $(CFLAGS) $(LRINC) $(ARMF) -static -no-pie -s \
		src/runner.c src/plat_stick.c src/pause.c src/gfx.c out/arm/libfceumm.a -o $@ -lm

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
out/%-arm: src/runner.c src/plat_stick.c src/*.h src/font_data.h out/arm/lib$$(EMU_$$*).a
	$(CROSS)g++ -x c $(CFLAGS) $(LRINC) $(ARMF) -c src/runner.c -o out/arm/runner.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/plat_stick.c -o out/arm/plat_stick.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/pause.c -o out/arm/pause.o
	$(CROSS)g++ -x c $(CFLAGS) $(ARMF) -c src/gfx.c -o out/arm/gfx.o
	$(CROSS)g++ $(ARMF) -static -no-pie -s out/arm/runner.o out/arm/plat_stick.o out/arm/pause.o out/arm/gfx.o out/arm/lib$(EMU_$*).a -o $@ -lm

out/%-pc: src/runner.c src/plat_sdl.c src/*.h src/font_data.h out/pc/lib$$(EMU_$$*).a
	c++ -x c $(CFLAGS) $(LRINC) -c src/runner.c -o out/pc/runner.o
	c++ -x c $(CFLAGS) $$(sdl2-config --cflags) -c src/plat_sdl.c -o out/pc/plat_sdl.o
	c++ -x c $(CFLAGS) -c src/pause.c -o out/pc/pause.o
	c++ -x c $(CFLAGS) -c src/gfx.c -o out/pc/gfx.o
	c++ out/pc/runner.o out/pc/plat_sdl.o out/pc/pause.o out/pc/gfx.o out/pc/lib$(EMU_$*).a -o $@ $$(sdl2-config --libs) -lm -lz

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

smoke-arm: out/menu-arm
	STICK_ROMS=roms STICK_SERIAL=/dev/null STICK_KEYS=R,D,D,A STICK_SHOT=out/stick-shot.ppm \
		qemu-arm-static ./out/menu-arm
	python3 -c "from PIL import Image; Image.open('out/stick-shot.ppm').save('out/stick-shot.png')"

.PHONY: all pc arm run smoke-arm

# make card -> out/card/retro ready to copy to the root of the SD card (FAT32 with a partition)
# menu music, effects and background: computed on the PC, the stick only reads them (tools/gen_audio.c)
out/gen-audio: tools/gen_audio.c src/music.c src/chip.c src/aero.c src/gfx.c src/*.h src/font_data.h
	cc $(CFLAGS) tools/gen_audio.c src/music.c src/chip.c src/aero.c src/gfx.c -o $@ -lm
out/audio/aero-background.bin: out/gen-audio
	mkdir -p out/audio && ./out/gen-audio out/audio
audio: out/audio/aero-background.bin

card: arm audio
	mkdir -p out/card && cp -r card-base/. out/card/ && mkdir -p out/card/retro/saves out/card/retro/audio
	cp out/menu-arm $(SYSTEMS:%=out/%-arm) out/nes-arm out/card/retro/
	cp out/audio/* out/card/retro/audio/
	@echo "copy out/card/retro to the root of the SD card"

.PHONY: card audio

# ---------- tests: no emulator core, no ROM, no window ----------
TESTF := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined -Isrc
TEST_MENU := src/gfx.c src/aero.c src/icons.c src/music.c tests/unit/plat_mock.c
TEST_BINS := out/test/test_gfx out/test/test_menu out/test/test_serial out/test/test_audio

out/test/test_gfx: tests/unit/test_gfx.c src/gfx.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_gfx.c src/gfx.c -o $@ -lm
out/test/test_menu: tests/unit/test_menu.c tests/unit/plat_mock.c src/*.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_menu.c $(TEST_MENU) -o $@ -lm
# the display structs hold 32-bit pointers on the stick: their size checks only apply to the ARM build
out/test/test_serial: tests/unit/test_serial.c src/plat_stick.c src/*.h
	@mkdir -p out/test
	cc $(TESTF) '-D_Static_assert(a,b)=' tests/unit/test_serial.c -o $@ -lm
out/test/test_audio: tests/unit/test_audio.c src/chip.c src/music.c src/*.h
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_audio.c src/chip.c src/music.c -o $@ -lm

SDLC = $$(sdl2-config --cflags)
SDLL = $$(sdl2-config --libs)
out/test/menu-sdl: $(SRC) src/plat_sdl.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(CFLAGS) $(SRC) src/plat_sdl.c -o $@ $(SDLC) $(SDLL) -lm
out/test/nes-pc: src/runner.c src/pause.c src/gfx.c src/plat_sdl.c tests/mock/mock_core.c tests/mock/libretro.h src/*.h $(FONT)
	@mkdir -p out/test
	cc $(CFLAGS) -Itests/mock src/runner.c src/pause.c src/gfx.c src/plat_sdl.c tests/mock/mock_core.c -o $@ $(SDLC) $(SDLL) -lm

test-unit: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do ./$$t; done
test-bins: out/test/menu-sdl out/test/nes-pc
test-e2e: test-bins
	$(PY) -m pytest tests/e2e
test-python:
	$(PY) -m pytest tests/python
test: test-unit test-python test-e2e

# README demo: mock games, no real title or artwork
demo: out/test/menu-sdl
	tools/make_demo_gif.sh

.PHONY: test test-unit test-bins test-e2e test-python demo
