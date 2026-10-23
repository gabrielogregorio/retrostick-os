# Hardware and factory system

The interfaces needed to work on the code. `firmware/` is not in the repo: if you want to inspect the device, it holds your own dump.

## Board

| Item | Value |
|---|---|
| SoC | **GeneralPlus GPL32900B** (the marking may be scratched or re-marked), ARM1176 core (ARMv6 + VFP) |
| RAM | 64 MB; the kernel uses `mem=55M`, 8 MB reserved for `gp_chunkmem` (video memory) |
| Flash | XMC **XM25QH64AIG**, 8 MB, SPI, 3.3 V (2.7 to 3.6 V) |
| Kernel | Linux 2.6.32.94 PREEMPT, HZ=100; glibc 2.11 in the rootfs |
| Clock | No battery-backed RTC: the date goes back to 1970 on every boot |
| Video | Logical panel **720x480** over HDMI at **59.94 Hz**, proprietary `gp_display` driver |
| Controllers | 2 wireless, through a receiver connected to **UART `/dev/ttyS1`** |

Images made for other processors (Hi3798, Rockchip) do not boot on this board.

**Binaries for the stick** must be static, built with musl and `-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=softfp -no-pie`.

## SPI flash (8 MB)

**Never write to it.** It is listed here only so a dump can be identified and inspected.

| Offset | Content |
|---|---|
| `0x000000` | `SPII` + "GPCV2149 SPI 1st bootloader" (several copies) |
| `0x020000` | `GPAP` + "GPL329xxB linux bootloader" |
| `0x031ba8` | zImage, Linux 2.6.32.94 |
| `0x14023c` | initramfs cpio.gz (7 MB uncompressed) |
| `0x4d0000` | Zeros up to `0x750000`, then data |

### Reading a dump (optional)

You do not need a dump to build or run this project. To identify a board:

- Use a CH341 programmer with a SOIC8 clip and `tools/re/read_spi.sh`, which only reads.
- Use flashrom 1.6 or newer, because older versions do not know the XM25QH64A. Build it from source if your distribution ships an older one, with only the `ch341a_spi` programmer enabled.
- **The black CH341B sends 5 V on its signal lines** (CS, CLK, MOSI, MISO), which is a risk to both the chip and the SoC. Prefer a green CH341A v1.7 with a voltage switch, or a Raspberry Pi Pico with pico-serprog.
- Never pass `-w` or `-E` to flashrom.

## Boot and the `retro/init` hook

The factory boot scripts do this:

1. Mount the card through mdev as vfat. exFAT would need `mount.exfat`, which does not exist on the stick, so in practice the card must be FAT32.
2. Look for `system.bin` (cramfs) in `/media/sdcard{a,b,c}{,1..4}` and mount it on `/system` if found.
3. If `/media/sdcarda{1,2,3}/retro` exists, mount it on `/usr/retro` and **run `/usr/retro/init` as root**.
4. Otherwise run the factory menu and do `rm -rf /media/sdcarda`, which is a danger for cards without a partition.

With `retro/init` on the card, the factory menu never starts and our script controls the device as root. The flash is never involved.

`SPI_ROM.bin` on the card makes the factory firmware rewrite the flash after "Press START". Never put a file with that name on the card.

The factory menu supervises its emulator process and starts another one if it stops. Do not run it alongside our menu.

## Video: `/dev/disp0` + `/dev/chunkmem`

| ioctl | What it does |
|---|---|
| `0x80046403` (disp) | Reads the panel resolution: `uint16 w, h` |
| `0xc00c4301` (chunkmem) | Allocates video memory: `{u32 phys; void *addr; u32 size}`; `addr` is the already mapped pointer |
| `0x402c6411` | Sets the primary layer bitmap (44-byte struct) |
| `0x402c6413` | Swaps the primary bitmap, for double buffering |
| `0x40046410` | Turns the primary layer on or off |
| `0x4004640a` | Applies the settings |
| `0x40046480` | Turns the **OSD** layer on or off. It sits above the primary, so turn it off on open |
| `0x6402` | Clears the "updated" mark and asks the hardware to pick up the new registers. Returns immediately |
| `0x6407` | **Sleeps until the frame interrupt marks "updated"** (50 ms timeout). After a swap and `0x6402`, this is the wait for the TV retrace |

Frame sequence: swap, `0x6402`, `0x6407`.

Bitmap (44 bytes):

```c
struct {
    u16 w;
    u16 h;
    u16 bpl;
    u8 type;      /* 0 = RGB565 */
    u8 pad;
    void *data;
    u8 rest[32];
};
```

**chunkmem is very slow to read.** Write to normal memory and copy to it in blocks.

**`gp_fb.ko` (the standard framebuffer) hangs the stick** at `insmod`. Never load it.

The stick also has hardware scalers, `/dev/scalar` and `/dev/scale2`. This project does not use them yet (see NEXT-STEPS.md).

## Audio: OSS on `/dev/dsp` (`gp_audio.ko`)

**Routing**

- The default output is analog (headphones/AV).
- `ioctl(dsp, 0xc0045007, &value)` with 1 switches to the HDMI I2S, with 0 back to analog.
- That ioctl rebuilds the fragments (8 x 23,520 B on HDMI), so it must come **before** `SETFRAGMENT`.
- Set the volume through `/dev/mixer` with `SOUND_MIXER_WRITE_VOLUME`: 0 to 100 per channel, which becomes 0 to 31 in the driver. Without the factory software running, nothing sets it for you.

**OSS behavior**

- The device accepts 48000 Hz and 32000 Hz, stereo, 16-bit.
- The blocking `write`, when the queue is full, waits with `msleep(1)` in a loop, up to 100 times. With HZ=100 each loop sleeps 10 to 20 ms, so the write releases in steps, not when space opens up.
- **Non-blocking works only through `ioctl SNDCTL_DSP_NONBLOCK` (0x500e).** The driver ignores `O_NONBLOCK` on `open`. With no space, the write returns what fit.
- `SNDCTL_DSP_GETODELAY` works (blocks in the DMA queue plus 1 block), and so does `GETOSPACE`.
- `SETFRAGMENT` is honored, with a ring of up to 128 blocks.
- DMA only starts with 2 blocks in the queue.
- With an empty queue the DMA repeats old leftovers, so fill the ring with silence before closing the device.

## Controllers: serial `/dev/ttyS1`, 115200 8N1

Configure the port as raw, 115200, from C (the stick has no `stty`). The receiver sends packets on its own, continuously, for each connected controller. The factory software does not need to be running.

```
0A 09 PP 32 31 ID B6 B7 CK
PP = 02 channel 1, 03 channel 2
ID = identifies the physical controller; bytes 3 and 4 are fixed
CK = ~(XOR(bytes 0..7) ^ 0x5A)
```

| Bit | B6 | B7 |
|---|---|---|
| 0 | Up | START |
| 1 | Down | SELECT |
| 2 | Left | L1 **and** L2 (same bit) |
| 3 | Right | R1 **and** R2 (same bit) |
| 4 | A | **MODE** |
| 5 | B | |
| 6 | Y | |
| 7 | X | |

- **Analog sticks send no position.** Bytes 3 to 5 never change, and half pushed is the same as fully pushed.
- The left stick becomes the D-pad, with 8 directions. A diagonal is two direction bits together, the same as a D-pad.
- The right stick sends **the same bit** as the physical button: up = Y, down = A, left = B, right = X. There is no way to tell pushing the stick from pressing the button.
- **L3 and R3 never arrive.**
- MODE does not switch any mode. It is a free button.
- This board uses only the UART receiver.

## The stick's busybox

Present: `ash, cat, cp, dd, ls, mkdir, mknod, mount, mv, ps, rm, sleep, sync, umount, usleep, kill, insmod, lsmod`; in `/usr/bin`: `killall, free, top, du, diff`.

Missing: `head, grep, dmesg, uname, stty, sed, awk`.

Boot scripts may use only what is present, plus `ash` builtins. Everything else goes in C.
