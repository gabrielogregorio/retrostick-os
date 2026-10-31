#!/usr/bin/env python3
"""Generates a super-compact cover + game details (year, genre, developer, description) for each game in roms/<system>/.

Sources (READ-ONLY, nothing in them is changed):
  - Game collection (RetroStation frontend): retro/cache/games.json (description + image name) and retro/assets/games/
  - libretro .rdb databases (RetroArch installed on the PC): year, genre, developer, matched by the ROM CRC
  - fixes file (--fixes, optional): manual corrections, see below

Output, per ROM, in roms/<system>/.meta/:
  <rom name without extension>.cap  60x86 cover, 16 colors with dithering, 4 bits per pixel (~2.6 KB):
                                    'CAP1', u16 width, u16 height, 16 x u16 RGB565 palette, pixels (high nibble first)
  <rom name without extension>.txt  line 1 year, 2 genre, 3 developer, 4 description (UTF-8)

Fixes file: an optional, user-owned JSON file of manual corrections, default cache/cover-fixes.json, skipped
silently if absent. It maps a ROM file name to an object with any of these keys:
  description  text that replaces the collection description
  year         release year
  image        image path relative to <collection>/assets/games/
  cover_name   catalog name to fetch from libretro-thumbnails
  source       "libretro": ignore the collection image and use the libretro cover
  no_cover     true: generate no cover (an existing one moves to trash/covers/)
  reason       free-form note, ignored by this script

Usage: python3 tools/gen_covers.py --collection <collection retro folder> [--rdb <.rdb folder>] [--preview file.png]
                                   [--fixes FILE]
"""
import argparse
import glob
import json
import os
import re
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Any

from PIL import Image, PngImagePlugin
from rdb import REGION_TAIL_RE, RdbCatalog, load_rdb, rdb_details

Record = dict[str, Any]
Cover = tuple[bytes, Image.Image]

PngImagePlugin.MAX_TEXT_CHUNK = 64 * 1024 * 1024  # some libretro covers carry a huge EXIF block

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COVER_CACHE = os.path.join(ROOT, 'cache', 'libretro-covers')
DEFAULT_RDB_GLOB = '~/.local/share/flatpak/app/org.libretro.RetroArch/*/stable/active/files/share/libretro/database/rdb'
THUMBNAILS_URL = 'https://thumbnails.libretro.com/'
USER_AGENT = 'retrostick-os/1'
DOWNLOAD_DELAY_SECS = 1
DOWNLOAD_TIMEOUT_SECS = 20
HTTP_NOT_FOUND = 404

COVER_W = 60  # at 4 bits per pixel a cover stays around 2.6 KB
COVER_H = 86
COLORS = 16
COVER_MAGIC = b'CAP1'
PREVIEW_GAP = 4
PREVIEW_SCALE = 2

EXTENSION_RE = re.compile(r'\.[a-z0-9]{1,4}$')
ARTICLE_RE = re.compile(r'^the |, the\b')
NON_ALPHANUMERIC_RE = re.compile(r'[^a-z0-9]')
UNSAFE_FILENAME_RE = re.compile(r'[&*/:`<>?\\|"]')
YEAR_RE = re.compile(r'\b(19[789]\d|20[0-2]\d)\b')

# menu system -> (.rdb databases, size of the header some ROMs have)
SYSTEMS = {
    'nes': (['Nintendo - Nintendo Entertainment System'], 16),
    'snes': (['Nintendo - Super Nintendo Entertainment System'], 512),
    'md': (['Sega - Mega Drive - Genesis'], 0),
    'sms': (['Sega - Master System - Mark III'], 0),
    'gba': (['Nintendo - Game Boy Advance'], 0),
    'gb': (['Nintendo - Game Boy', 'Nintendo - Game Boy Color'], 0),
    'gg': (['Sega - Game Gear'], 0),
    'a26': (['Atari - 2600'], 0),
    'ngp': (['SNK - Neo Geo Pocket Color', 'SNK - Neo Geo Pocket'], 0),
    'ws': (['Bandai - WonderSwan Color', 'Bandai - WonderSwan'], 0),
}

# prefix of the collection folders of each system
FOLDERS = {
    'nes': ('NES',),
    'snes': ('SFC', 'SMC'),
    'md': ('MD', 'BIN_MEGA', 'ZIP_BIN_MEGA'),
    'sms': ('SMS',),
    'gba': ('GBA',),
    'gb': ('GB_', 'GBC', 'SGB'),
    'gg': ('GG_',),
    'a26': ('A26', 'BIN_ATARI', 'ZIP_A26'),
    'ngp': ('NGC', 'NGP'),
    'ws': ('WS_', 'WSC'),
}

GENRES = {  # libretro genres -> label shown on the game details
    'Action': 'Action',
    'Platform': 'Platform',
    'Shooter': 'Shooter',
    "Beat'em Up": "Beat 'em up",
    'Role-playing (RPG)': 'RPG',
    'Sports': 'Sports',
    'Racing': 'Racing',
    'Puzzle': 'Puzzle',
    'Fighting': 'Fighting',
    'Adventure': 'Adventure',
    'Strategy': 'Strategy',
    'Simulation': 'Simulation',
    "Shoot'em Up": "Shoot 'em up",
    'Boxing': 'Boxing',
    'Board game': 'Board game',
    'Pinball': 'Pinball',
}


@dataclass
class Collection:
    root: str
    by_file: dict[str, list[Record]] = field(default_factory=dict)  # some names repeat across systems
    by_title: dict[tuple[str, str], list[Record]] = field(default_factory=dict)  # (system, title key) -> records


@dataclass
class Totals:
    games: int = 0
    cover_bytes: int = 0
    previews: list[Image.Image] = field(default_factory=list)


@dataclass
class SystemJob:
    name: str
    bases: list[str]
    header: int
    folder: str
    meta: str
    catalog: RdbCatalog


class CoverDownloader:
    """libretro-thumbnails covers by catalog name (No-Intro).

    Local cache; one request at a time; stops at the first block.
    """

    def __init__(self, network: bool) -> None:
        self.network = network
        self.blocked = False

    def fetch(self, base: str, name: str) -> str:
        filename = UNSAFE_FILENAME_RE.sub('_', name) + '.png'
        local = os.path.join(COVER_CACHE, base, filename)
        if os.path.exists(local):
            return local
        if self.blocked or not self.network:
            return ''
        data = self._download(base, filename, name)
        if data is None:
            return ''
        os.makedirs(os.path.dirname(local), exist_ok=True)
        with open(local, 'wb') as file:
            file.write(data)
        print(f'    downloaded: {name}')

        return local

    def _download(self, base: str, filename: str, name: str) -> bytes | None:
        url = THUMBNAILS_URL + urllib.parse.quote(base) + '/Named_Boxarts/' + urllib.parse.quote(filename)
        request = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
        try:
            time.sleep(DOWNLOAD_DELAY_SECS)
            with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT_SECS) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code == HTTP_NOT_FOUND:
                print(f'    (libretro has no cover: {name})')

                return None
            print(f'    STOPPING downloads: HTTP {error.code} on {url}')
        except Exception as error:
            print(f'    STOPPING downloads: {error}')
        self.blocked = True

        return None


def title_key(name: str | None) -> str:
    """Title without region/version/extension, article or punctuation: 'Some Game, The (USA).sfc' -> 'somegame'."""
    title = EXTENSION_RE.sub('', (name or '').lower())
    title = REGION_TAIL_RE.sub('', title).replace('pokémon', 'pokemon').replace('&', 'and').replace('_', ' ')
    title = ARTICLE_RE.sub(' ', title)

    return NON_ALPHANUMERIC_RE.sub('', title)


def crop_to_cover_ratio(image: Image.Image) -> Image.Image:
    """Covers the whole cover format, cutting the excess at the center."""
    target = COVER_W / COVER_H
    width, height = image.size
    if width / height > target:
        new_width = int(height * target)
        left = (width - new_width) // 2

        return image.crop((left, 0, left + new_width, height))
    new_height = int(width / target)
    top = (height - new_height) // 2

    return image.crop((0, top, width, top + new_height))


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def make_cover(path: str) -> Cover:
    image = crop_to_cover_ratio(Image.open(path).convert('RGB')).resize((COVER_W, COVER_H), Image.LANCZOS)
    palette_image = image.quantize(COLORS, method=Image.Quantize.MEDIANCUT)
    quantized = image.quantize(palette=palette_image, dither=Image.Dither.FLOYDSTEINBERG)
    palette = quantized.getpalette()[:COLORS * 3] + [0] * (COLORS * 3)
    palette565 = [rgb565(*palette[index * 3:index * 3 + 3]) for index in range(COLORS)]
    pixels = quantized.tobytes()
    packed = bytes((pixels[index] << 4) | pixels[index + 1] for index in range(0, len(pixels), 2))
    data = COVER_MAGIC + struct.pack('<HH', COVER_W, COVER_H) + struct.pack(f'<{COLORS}H', *palette565) + packed

    return data, quantized.convert('RGB')


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument('--collection', required=True, help='retro/ folder of a game collection in RetroStation format')
    parser.add_argument('--rdb', default=None)
    parser.add_argument('--preview', default=None, help='saves a sheet with all the generated covers')
    parser.add_argument('--download', action='store_true',
                        help='fetches from libretro-thumbnails the covers the collection lacks')
    parser.add_argument('--fixes', default=os.path.join(ROOT, 'cache', 'cover-fixes.json'),
                        help='optional JSON file of manual corrections (default cache/cover-fixes.json)')

    return parser.parse_args()


def game_file_path(game_file: Record) -> str:
    return game_file.get('gameRelativePath') or game_file.get('gameRelativePathOrFolder') or ''


def title_keys(game: Record) -> set[str]:
    keys = {title_key(game.get('name', ''))}
    for game_file in game.get('files', []):
        keys.add(title_key(os.path.basename(game_file_path(game_file))))

    return keys


def load_collection(root: str) -> Collection:
    with open(os.path.join(root, 'cache/games.json')) as file:
        games = json.load(file)
    collection = Collection(root)
    for game in games:
        for game_file in game.get('files', []):
            collection.by_file.setdefault(os.path.basename(game_file_path(game_file)), []).append(game)
    for game in games:
        for system, prefixes in FOLDERS.items():
            if not game['folder'].startswith(prefixes):
                continue
            for key in title_keys(game):
                if key:
                    collection.by_title.setdefault((system, key), []).append(game)

    return collection


def load_fixes(path: str) -> dict[str, Record]:
    if not os.path.exists(path):
        return {}
    with open(path) as file:
        return json.load(file)


def pick_record(rom: str, system: str, collection: Collection) -> Record:
    """Prefers the record from the system folder and with an image."""
    prefixes = FOLDERS[system]
    candidates = collection.by_file.get(rom, [])
    if not any(record['folder'].startswith(prefixes) for record in candidates):
        # copy renamed to the catalog name: look it up by title in the system folder
        candidates = collection.by_title.get((system, title_key(rom)), [])

    def preference(record: Record) -> tuple[bool, bool, bool]:
        return record['folder'].startswith(prefixes), bool(record.get('image')), bool(record.get('description'))

    return max(candidates, key=preference, default={})


def resolve_year(fix: Record, details: Record, description: str) -> int | str:
    year = fix.get('year', details.get('releaseyear'))
    if year:
        return year
    found = YEAR_RE.search(description)

    return int(found.group(1)) if found else ''


def collection_image(record: Record, fix: Record, root: str) -> str:
    relative = fix.get('image') or record.get('image')
    if relative:
        return os.path.join(root, 'assets/games', relative)
    optimized_name = record.get('searchNameOptimized')
    if not optimized_name:
        return ''
    # no image in the record: looks for <optimized name>[digits].webp/jpg in the system folder
    image_dir = os.path.join(root, 'assets/games', record['folder'])
    pattern = re.compile(re.escape(optimized_name) + r'\d*\.(webp|jpg|png)$')
    names = os.listdir(image_dir) if os.path.isdir(image_dir) else []
    matches = sorted(name for name in names if pattern.match(name))

    return os.path.join(image_dir, matches[0]) if matches else ''


def retire_cover(job: SystemJob, base: str) -> None:
    """No rm: the wrong cover goes to trash/."""
    old_cover = os.path.join(job.meta, base + '.cap')
    if not os.path.exists(old_cover):
        return
    os.makedirs(os.path.join(ROOT, 'trash', 'covers'), exist_ok=True)
    os.replace(old_cover, os.path.join(ROOT, 'trash', 'covers', job.name + '-' + base + '.cap'))


def find_cover_image(image: str, fix: Record, catalog_name: str | None, base_db: str,
                     downloader: CoverDownloader) -> str:
    if fix.get('source') == 'libretro':
        image = ''  # wrong collection cover: use the libretro one
    if image and os.path.exists(image):
        return image
    if not catalog_name and not fix.get('cover_name'):
        return ''

    return downloader.fetch(base_db, fix.get('cover_name', catalog_name))


def read_cover(image: str) -> Cover | None:
    if not image or not os.path.exists(image):
        return None
    try:
        return make_cover(image)
    except Exception as error:
        print(f'    unreadable image ({error}): {image}')

        return None


def report_rom(base: str, year: int | str, genre: str, size: int, description: str, catalog_name: str | None) -> None:
    cover_note = f'cover {size} B' if size else 'NO COVER'
    print(f'  {base[:40]:40} {year!s:5} {genre[:14]:14} {cover_note}'
          f'{"" if description else "  NO DESCRIPTION"}{"" if catalog_name else "  (not in libretro database)"}')


def process_rom(rom: str, job: SystemJob, collection: Collection, fixes: dict[str, Record],
                downloader: CoverDownloader, totals: Totals) -> None:
    base = os.path.splitext(rom)[0]
    record = pick_record(rom, job.name, collection)
    catalog_name, details = rdb_details(os.path.join(job.folder, rom), job.header, job.catalog)
    fix = fixes.get(rom, {})
    description = fix.get('description', (record.get('description') or '').strip())
    year = resolve_year(fix, details, description)
    genre = GENRES.get(details.get('genre', ''), details.get('genre', ''))
    developer = details.get('developer', '') or details.get('publisher', '')
    with open(os.path.join(job.meta, base + '.txt'), 'w', encoding='utf-8') as file:
        file.write(f'{year}\n{genre}\n{developer}\n{" ".join(description.split())}\n')
    image = collection_image(record, fix, collection.root)
    if fix.get('no_cover'):
        retire_cover(job, base)
        cover = None
    else:
        image = find_cover_image(image, fix, catalog_name, details.get('_base', job.bases[0]), downloader)
        cover = read_cover(image)
    size = 0
    if cover:
        data, preview = cover
        with open(os.path.join(job.meta, base + '.cap'), 'wb') as file:
            file.write(data)
        size = len(data)
        totals.cover_bytes += size
        totals.previews.append(preview)
    totals.games += 1
    report_rom(base, year, genre, size, description, catalog_name)


def process_system(system: str, rdb_folder: str, collection: Collection, fixes: dict[str, Record],
                   downloader: CoverDownloader, totals: Totals) -> None:
    bases, header = SYSTEMS[system]
    folder = os.path.join(ROOT, 'roms', system)
    entries = os.listdir(folder) if os.path.isdir(folder) else []
    roms = sorted(entry for entry in entries if not entry.startswith('.'))
    if not roms:
        return
    print(f'{system}: {len(roms)} games')
    catalog = load_rdb(rdb_folder, bases) if rdb_folder else RdbCatalog()
    meta = os.path.join(folder, '.meta')
    os.makedirs(meta, exist_ok=True)
    job = SystemJob(system, bases, header, folder, meta, catalog)
    for rom in roms:
        process_rom(rom, job, collection, fixes, downloader, totals)


def save_preview_sheet(previews: list[Image.Image], path: str) -> None:
    sheet = Image.new('RGB', (len(previews) * (COVER_W + PREVIEW_GAP), COVER_H), 'white')
    for index, preview in enumerate(previews):
        sheet.paste(preview, (index * (COVER_W + PREVIEW_GAP), 0))
    sheet.resize((sheet.width * PREVIEW_SCALE, sheet.height * PREVIEW_SCALE), Image.NEAREST).save(path)


def main() -> None:
    args = parse_args()
    rdb_folder = args.rdb or (glob.glob(os.path.expanduser(DEFAULT_RDB_GLOB)) + [''])[0]
    print(f'Collection: {args.collection}\nrdb: {rdb_folder}')
    collection = load_collection(args.collection)
    fixes = load_fixes(args.fixes)
    downloader = CoverDownloader(args.download)
    totals = Totals()
    for system in SYSTEMS:
        process_system(system, rdb_folder, collection, fixes, downloader, totals)
    print(f'{totals.games} game details, {len(totals.previews)} covers, {totals.cover_bytes} cover bytes in total')
    if args.preview and totals.previews:
        save_preview_sheet(totals.previews, args.preview)


if __name__ == '__main__':
    main()
