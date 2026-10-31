#!/bin/bash
# Reads the stick SPI flash with a CH341. READ-ONLY: never uses -w, -E or write -v.
# Usage: read_spi.sh            -> XM25QH64A (the stick chip): identifies it and reads twice
#        read_spi.sh XM25QH64C  -> forces the model (when flashrom finds more than one)
set -u
DEST=${DEST:-$PWD/spi-dump}
mkdir -p "$DEST"
chip=${1:-XM25QH64A}
flashrom_bin=${FLASHROM:-flashrom} # v2.0-devel, knows the XM25QH64A
flashrom_options=(-p ch341a_spi)
if [ -n "$chip" ]; then
  flashrom_options+=(-c "$chip")
fi
cd "$DEST" || exit 1
stamp=$(date +%Y%m%d-%H%M%S)

echo "== [1/3] identifying the chip ($(date +%T))"
"$flashrom_bin" "${flashrom_options[@]}" 2>&1 | tee "probe-$stamp.log"
if ! grep -q "Found .* flash chip" "probe-$stamp.log"; then
  echo "!! chip not found. UNPLUG the CH341B and check the log: $DEST/probe-$stamp.log"
  exit 2
fi
if grep -q "Multiple flash chip definitions" "probe-$stamp.log"; then
  echo "!! more than one possible model. Run again passing the name, e.g.: $0 XM25QH64C"
  exit 3
fi

for read_number in 1 2; do
  echo "== [$((read_number + 1))/3] read $read_number ($(date +%T))"
  "$flashrom_bin" "${flashrom_options[@]}" -r "read$read_number-$stamp.bin" 2>&1 | tail -3
done

echo "== checking"
sha256sum read?-"$stamp".bin | tee "sha256-$stamp.txt"
first_hash=$(sha256sum "read1-$stamp.bin" | cut -d' ' -f1)
second_hash=$(sha256sum "read2-$stamp.bin" | cut -d' ' -f1)
if [ "$first_hash" = "$second_hash" ]; then
  echo "OK: the 2 reads are identical. You can UNPLUG the CH341B."
else
  echo "!! reads differ (bad contact). UNPLUG and reseat the clip before trying again."
fi
