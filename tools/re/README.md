# Analysis tools

Optional tools for inspecting your own device. You do not need them to build or run this project. All of them only read, and `read_spi.sh` never writes to the flash.

`armdis.py` and `kodis.py` need a venv:

```bash
python3 -m venv tools/re/venv
tools/re/venv/bin/pip install capstone pyelftools
```

## `armdis.py`

Disassembles functions from a non-stripped ARM ELF and annotates PLT calls, strings and literals.

```bash
tools/re/venv/bin/python tools/re/armdis.py <elf file> --list <text>   # list matching functions
tools/re/venv/bin/python tools/re/armdis.py <elf file> <function>      # disassemble one function
```

## `kodis.py`

Disassembles a function from a kernel module (`.ko`) with relocations resolved, showing the name of each called function and the value of each literal.

```bash
tools/re/venv/bin/python tools/re/kodis.py <module.ko> <function>
```

## `read_spi.sh`

Reads the SPI flash with a CH341, twice, and prints the sha256 of each read so you can compare them. It never uses `-w` or `-E`.

```bash
tools/re/read_spi.sh              # XM25QH64A, the chip on this board
tools/re/read_spi.sh XM25QH64C    # force another model if flashrom finds more than one
```

- It needs flashrom 1.6 or newer. Set the `FLASHROM` environment variable if it is not on the `PATH`.
- The output folder is `spi-dump/` in the current directory, or the `DEST` environment variable.
- See `docs/HARDWARE.md` for the warning about 5 V on the black CH341B.

## `fat32_map.py`

Prints a file map of a partial FAT32 image.

```bash
python3 tools/re/fat32_map.py <image> [partition offset in bytes]
```
