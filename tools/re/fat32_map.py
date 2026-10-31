#!/usr/bin/env python3
"""FAT32 map of a partial image (read-only). Lists files with their offset in the image.
usage: fat32_map.py image [part_offset] > map.tsv  (path, size, start_offset, max_end_offset, fragments)"""
import struct
import sys
from dataclasses import dataclass
from typing import BinaryIO

DEFAULT_PARTITION_OFFSET = 1048576
BOOT_SECTOR_SIZE = 512
BPB_GEOMETRY_OFFSET = 11
BPB_FAT_SIZE_OFFSET = 36
BPB_ROOT_CLUSTER_OFFSET = 44

FIRST_DATA_CLUSTER = 2
CLUSTER_MASK = 0x0FFFFFFF
CLUSTER_END = 0x0FFFFFF7
MAX_CHAIN_LENGTH = 2_000_000
FAT_ENTRY_SIZE = 4

DIR_ENTRY_SIZE = 32
ENTRY_FREE = 0
ENTRY_DELETED = 0xE5
ATTRIBUTES_OFFSET = 11
ATTRIBUTE_LONG_NAME = 0x0F
ATTRIBUTE_VOLUME_LABEL = 0x08
ATTRIBUTE_DIRECTORY = 0x10
CLUSTER_HIGH_OFFSET = 20
CLUSTER_LOW_OFFSET = 26
FILE_SIZE_OFFSET = 28


@dataclass
class Fat32:
    image: BinaryIO
    image_size: int
    cluster_size: int
    data_start: int
    fat: bytes

    def read(self, offset: int, length: int) -> bytes:
        self.image.seek(offset)

        return self.image.read(length)

    def chain(self, cluster: int) -> list[int]:
        clusters: list[int] = []
        while FIRST_DATA_CLUSTER <= cluster < CLUSTER_END and len(clusters) < MAX_CHAIN_LENGTH:
            clusters.append(cluster)
            cluster = struct.unpack_from('<I', self.fat, cluster * FAT_ENTRY_SIZE)[0] & CLUSTER_MASK

        return clusters

    def cluster_offset(self, cluster: int) -> int:
        return self.data_start + (cluster - FIRST_DATA_CLUSTER) * self.cluster_size

    def read_chain(self, cluster: int) -> bytes:
        content = b''
        for link in self.chain(cluster):
            offset = self.cluster_offset(link)
            if offset + self.cluster_size > self.image_size:
                raise EOFError(offset)
            content += self.read(offset, self.cluster_size)

        return content


def open_volume(image: BinaryIO, partition: int) -> tuple[Fat32, int]:
    image_size = image.seek(0, 2)
    image.seek(partition)
    boot = image.read(BOOT_SECTOR_SIZE)
    bytes_per_sector, sectors_per_cluster, reserved, fat_count = struct.unpack_from('<HBHB', boot, BPB_GEOMETRY_OFFSET)
    fat_sectors = struct.unpack_from('<I', boot, BPB_FAT_SIZE_OFFSET)[0]
    root_cluster = struct.unpack_from('<I', boot, BPB_ROOT_CLUSTER_OFFSET)[0]
    fat_start = partition + reserved * bytes_per_sector
    data_start = fat_start + fat_count * fat_sectors * bytes_per_sector
    image.seek(fat_start)
    fat = image.read(fat_sectors * bytes_per_sector)
    volume = Fat32(image, image_size, bytes_per_sector * sectors_per_cluster, data_start, fat)

    return volume, root_cluster


def entry_name(entry: bytes, long_name_parts: list[bytes]) -> str:
    if long_name_parts:
        return b''.join(long_name_parts).decode('utf-16le', 'ignore').split('\x00')[0]
    stem = entry[0:8].decode('latin1').strip()
    extension = entry[8:11].decode('latin1').strip()

    return stem + ('.' + extension if extension else '')


def print_file(volume: Fat32, path: str, size: int, first_cluster: int) -> None:
    clusters = volume.chain(first_cluster) if first_cluster else []
    fragments = 1 + sum(1 for link, following in zip(clusters, clusters[1:], strict=False) if following != link + 1)
    start = volume.cluster_offset(clusters[0]) if clusters else 0
    end = max(volume.cluster_offset(link) for link in clusters) + volume.cluster_size if clusters else 0
    print(f'{path}\t{size}\t{start}\t{end}\t{fragments}')


def visit_entry(volume: Fat32, entry: bytes, name: str, parent: str) -> None:
    attributes = entry[ATTRIBUTES_OFFSET]
    if name in ('.', '..') or attributes & ATTRIBUTE_VOLUME_LABEL:
        return
    high = struct.unpack_from('<H', entry, CLUSTER_HIGH_OFFSET)[0]
    low = struct.unpack_from('<H', entry, CLUSTER_LOW_OFFSET)[0]
    cluster = (high << 16) | low
    size = struct.unpack_from('<I', entry, FILE_SIZE_OFFSET)[0]
    path = f'{parent}/{name}' if parent else name
    if attributes & ATTRIBUTE_DIRECTORY:
        print(f'{path}/\tDIR\t{volume.cluster_offset(cluster)}')
        walk(volume, cluster, path)

        return
    print_file(volume, path, size, cluster)


def walk(volume: Fat32, cluster: int, parent: str) -> None:
    try:
        directory = volume.read_chain(cluster)
    except EOFError as error:
        print(f'{parent}/\tDIR-OUTSIDE\t{error}', file=sys.stderr)

        return
    long_name_parts: list[bytes] = []
    for start in range(0, len(directory), DIR_ENTRY_SIZE):
        entry = directory[start:start + DIR_ENTRY_SIZE]
        if entry[0] == ENTRY_FREE:
            return
        if entry[0] == ENTRY_DELETED:
            long_name_parts = []
            continue
        if entry[ATTRIBUTES_OFFSET] == ATTRIBUTE_LONG_NAME:
            long_name_parts.insert(0, entry[1:11] + entry[14:26] + entry[28:32])
            continue
        name = entry_name(entry, long_name_parts)
        long_name_parts = []
        visit_entry(volume, entry, name, parent)


def main() -> None:
    partition = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_PARTITION_OFFSET
    with open(sys.argv[1], 'rb') as image:
        volume, root_cluster = open_volume(image, partition)
        print(f'# cs={volume.cluster_size} data0={volume.data_start} root={root_cluster} '
              f'imgsize_partial={volume.image_size}', file=sys.stderr)
        walk(volume, root_cluster, '')


if __name__ == '__main__':
    main()
