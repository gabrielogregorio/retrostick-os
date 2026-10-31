# make pc    -> out/menu-pc (window on the PC)
# make arm   -> out/menu-arm (static binary for the stick: ARMv6 + VFP, musl)
# make run   -> opens the preview on the PC
# make smoke-arm -> runs the ARM menu under qemu-arm, headless, and saves out/stick-shot.png
# make test  -> unit and e2e tests (no emulator core, no ROM, no window)
# make demo  -> records docs/demo.gif from the menu on mock games
ARMF := -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp
CROSS ?= toolchain/arm-linux-musleabi-cross/bin/arm-linux-musleabi-
MENU_SRC := src/catalog.c src/details.c src/menu_audio.c src/menu_view.c
SRC := src/main.c $(MENU_SRC) src/gfx.c src/aero.c src/icons.c src/music.c src/plat_exec.c
STICK_SRC := src/plat_stick.c src/stick_video.c src/stick_frames.c src/stick_input.c src/stick_audio.c
RUNNER_SRC := src/runner.c src/runner_audio.c src/runner_saves.c src/runner_pause.c src/pause.c src/gfx.c
FONT := src/font_data.h
PY ?= python3
CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

all: pc arm

# Generated at build time from the system Noto Sans, not stored in the repo.
src/font_data.h: tools/gen_font.py
	$(PY) tools/gen_font.py

out/menu-pc: $(SRC) src/plat_sdl.c src/*.h src/font_data.h
	@mkdir -p out
	cc $(CFLAGS) $(SRC) src/plat_sdl.c -o $@ $$(sdl2-config --cflags --libs) -lm

out/menu-arm: $(SRC) $(STICK_SRC) src/*.h src/font_data.h
	@mkdir -p out
	$(CROSS)gcc $(CFLAGS) $(ARMF) -static -no-pie -s $(SRC) $(STICK_SRC) -o $@ -lm

LRINC := -Icores/fceumm/src/drivers/libretro/libretro-common/include

# compiled as C through the C++ driver because some cores need the C++ runtime at link time
define link_runner_pc
	@mkdir -p out/pc
	set -e; for f in $(RUNNER_SRC) src/plat_sdl.c; do \
		c++ -x c $(CFLAGS) $(LRINC) $$(sdl2-config --cflags) -c $$f -o out/pc/$$(basename $$f .c).o; done
	c++ $(RUNNER_SRC:src/%.c=out/pc/%.o) out/pc/plat_sdl.o $(1) -o $@ $$(sdl2-config --libs) -lm -lz
endef
define link_runner_arm
	@mkdir -p out/arm
	set -e; for f in $(RUNNER_SRC) $(STICK_SRC); do \
		$(CROSS)g++ -x c $(CFLAGS) $(LRINC) $(ARMF) -c $$f -o out/arm/$$(basename $$f .c).o; done
	$(CROSS)g++ $(ARMF) -static -no-pie -s $(RUNNER_SRC:src/%.c=out/arm/%.o) $(STICK_SRC:src/%.c=out/arm/%.o) $(1) -o $@ -lm
endef

# NES = QuickNES (light). FCEUmm stays as nes-fceumm-arm, more accurate and heavier
# (on the stick its emulation does not fit in the 16.6 ms of a frame at 60 Hz).
out/nes-pc: $(RUNNER_SRC) src/plat_sdl.c src/*.h src/font_data.h out/pc/libquicknes.a
	$(call link_runner_pc,out/pc/libquicknes.a)

out/nes-arm: $(RUNNER_SRC) $(STICK_SRC) src/*.h src/font_data.h out/arm/libquicknes.a
	$(call link_runner_arm,out/arm/libquicknes.a)

out/nes-fceumm-arm: $(RUNNER_SRC) $(STICK_SRC) src/*.h src/font_data.h out/arm/libfceumm.a
	$(call link_runner_arm,out/arm/libfceumm.a)

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
out/%-arm: $(RUNNER_SRC) $(STICK_SRC) src/*.h src/font_data.h out/arm/lib$$(EMU_$$*).a
	$(call link_runner_arm,out/arm/lib$(EMU_$*).a)

out/%-pc: $(RUNNER_SRC) src/plat_sdl.c src/*.h src/font_data.h out/pc/lib$$(EMU_$$*).a
	$(call link_runner_pc,out/pc/lib$(EMU_$*).a)

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
	@mkdir -p out
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
TEST_MENU := $(MENU_SRC) src/gfx.c src/aero.c src/icons.c src/music.c tests/unit/plat_mock.c
TEST_BINS := out/test/test_gfx out/test/test_menu out/test/test_serial out/test/test_audio

out/test/test_gfx: tests/unit/test_gfx.c src/gfx.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_gfx.c src/gfx.c -o $@ -lm
out/test/test_menu: tests/unit/test_menu.c tests/unit/plat_mock.c src/*.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_menu.c $(TEST_MENU) -o $@ -lm
# the display structs hold 32-bit pointers on the stick: their size checks only apply to the ARM build
out/test/test_serial: tests/unit/test_serial.c $(STICK_SRC) src/*.h
	@mkdir -p out/test
	cc $(TESTF) '-D_Static_assert(a,b)=' tests/unit/test_serial.c $(STICK_SRC) -o $@ -lm
out/test/test_audio: tests/unit/test_audio.c src/chip.c src/music.c src/*.h
	@mkdir -p out/test
	cc $(TESTF) tests/unit/test_audio.c src/chip.c src/music.c -o $@ -lm

SDLC = $$(sdl2-config --cflags)
SDLL = $$(sdl2-config --libs)
out/test/menu-sdl: $(SRC) src/plat_sdl.c src/*.h $(FONT)
	@mkdir -p out/test
	cc $(CFLAGS) $(SRC) src/plat_sdl.c -o $@ $(SDLC) $(SDLL) -lm
out/test/nes-pc: $(RUNNER_SRC) src/plat_sdl.c tests/mock/mock_core.c tests/mock/libretro.h src/*.h $(FONT)
	@mkdir -p out/test
	cc $(CFLAGS) -Itests/mock $(RUNNER_SRC) src/plat_sdl.c tests/mock/mock_core.c -o $@ $(SDLC) $(SDLL) -lm

WARN := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers
check-warnings: $(FONT)
	cc -fsyntax-only $(WARN) -Isrc $(SDLC) $(SRC) src/chip.c src/plat_sdl.c tools/gen_audio.c
	cc -fsyntax-only $(WARN) -Isrc -Itests/mock $(RUNNER_SRC) tests/mock/mock_core.c
	cc -fsyntax-only $(WARN) '-D_Static_assert(a,b)=' -Isrc $(STICK_SRC)

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

.PHONY: check-warnings test test-unit test-bins test-e2e test-python demo
