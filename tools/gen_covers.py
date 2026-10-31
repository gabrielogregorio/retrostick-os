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
import sys
import time
import urllib.parse
import urllib.request
import zlib

from PIL import Image, PngImagePlugin

PngImagePlugin.MAX_TEXT_CHUNK = 64 * 1024 * 1024  # some libretro covers carry a huge EXIF block

sys.path.insert(0, os.path.dirname(__file__))
from rdb import read_rdb  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COVER_W, COVER_H, COLORS = 60, 86, 16  # at 4 bits per pixel a cover stays around 2.6 KB

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
FOLDERS = {'nes': ('NES',), 'snes': ('SFC', 'SMC'), 'md': ('MD', 'BIN_MEGA', 'ZIP_BIN_MEGA'), 'sms': ('SMS',),
           'gba': ('GBA',), 'gb': ('GB_', 'GBC', 'SGB'), 'gg': ('GG_',), 'a26': ('A26', 'BIN_ATARI', 'ZIP_A26'),
           'ngp': ('NGC', 'NGP'), 'ws': ('WS_', 'WSC')}

GENRES = {  # libretro genres -> label shown on the game details
    'Action': 'Action', 'Platform': 'Platform', 'Shooter': 'Shooter', "Beat'em Up": "Beat 'em up",
    'Role-playing (RPG)': 'RPG', 'Sports': 'Sports', 'Racing': 'Racing', 'Puzzle': 'Puzzle',
    'Fighting': 'Fighting', 'Adventure': 'Adventure', 'Strategy': 'Strategy', 'Simulation': 'Simulation',
    'Shoot\'em Up': "Shoot 'em up", 'Boxing': 'Boxing', 'Board game': 'Board game', 'Pinball': 'Pinball',
}


def title_key(name):
    """Title without region/version/extension, article or punctuation: 'Some Game, The (USA).sfc' -> 'somegame'."""
    t = re.sub(r'\.[a-z0-9]{1,4}$', '', (name or '').lower())
    t = re.sub(r'\s*[\(\[].*$', '', t).replace('pokémon', 'pokemon').replace('&', 'and').replace('_', ' ')
    t = re.sub(r'^the |, the\b', ' ', t)
    return re.sub(r'[^a-z0-9]', '', t)


def base_title(name):
    return re.sub(r'\s*[\(\[].*$', '', name or '').strip().lower()


def load_rdb(folder, bases):
    by_crc, by_name, by_base = {}, {}, {}
    for b in bases:
        path = os.path.join(folder, b + '.rdb')
        if not os.path.exists(path):
            print(f'  warning: missing {path}')
            continue
        for x in read_rdb(path):
            n = x.get('name')
            if isinstance(x.get('crc'), bytes) and n:
                by_crc.setdefault(x['crc'].hex(), (n, b))
            if n:
                m = by_name.setdefault(n, {})
                for k in ('releaseyear', 'genre', 'developer', 'publisher'):
                    if k in x and k not in m:
                        m[k] = x[k]
                if 'releaseyear' in x:
                    tb = base_title(n)
                    by_base[tb] = min(by_base.get(tb, 9999), x['releaseyear'])
    return by_crc, by_name, by_base


def rdb_details(rom, header, by_crc, by_name, by_base):
    b = open(rom, 'rb').read()
    candidates = [b]
    if header and len(b) % 1024 == header % 1024 and len(b) > header:
        candidates.append(b[header:])
    elif header == 16 and b[:4] == b'NES\x1a':
        candidates.append(b[16:])
    for c in candidates:
        found = by_crc.get(format(zlib.crc32(c), '08x'))
        if found:
            n, base = found
            m = dict(by_name.get(n, {}))
            if 'releaseyear' not in m and base_title(n) in by_base:
                m['releaseyear'] = by_base[base_title(n)]
            m['_base'] = base
            return n, m
    return None, {}


CACHE = os.path.join(ROOT, 'cache', 'libretro-covers')
blocked = False


def download_cover(base, name, network=True):
    """libretro-thumbnails cover by catalog name (No-Intro). Local cache; one request at a time; stops at the first block."""
    global blocked
    filename = re.sub(r'[&*/:`<>?\\|"]', '_', name) + '.png'
    local = os.path.join(CACHE, base, filename)
    if os.path.exists(local):
        return local
    if blocked or not network:
        return ''
    url = 'https://thumbnails.libretro.com/' + urllib.parse.quote(base) + '/Named_Boxarts/' + urllib.parse.quote(filename)
    try:
        time.sleep(1)
        with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'retrostick/1'}), timeout=20) as r:
            data = r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print(f'    (libretro has no cover: {name})')
            return ''
        print(f'    STOPPING downloads: HTTP {e.code} on {url}')
        blocked = True
        return ''
    except Exception as e:
        print(f'    STOPPING downloads: {e}')
        blocked = True
        return ''
    os.makedirs(os.path.dirname(local), exist_ok=True)
    open(local, 'wb').write(data)
    print(f'    downloaded: {name}')
    return local


def make_cover(path):
    im = Image.open(path).convert('RGB')
    # crops to the cover format (covers the area, cuts the excess at the center)
    target = COVER_W / COVER_H
    w, h = im.size
    if w / h > target:
        nw = int(h * target); im = im.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:
        nh = int(w / target); im = im.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    im = im.resize((COVER_W, COVER_H), Image.LANCZOS)
    pal = im.quantize(COLORS, method=Image.Quantize.MEDIANCUT)
    q = im.quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG)
    p = q.getpalette()[:COLORS * 3] + [0] * (COLORS * 3)
    pal565 = [((p[i * 3] >> 3) << 11) | ((p[i * 3 + 1] >> 2) << 5) | (p[i * 3 + 2] >> 3) for i in range(COLORS)]
    px = q.tobytes()
    pack = bytes((px[i] << 4) | px[i + 1] for i in range(0, len(px), 2))
    data = b'CAP1' + struct.pack('<HH', COVER_W, COVER_H) + struct.pack('<16H', *pal565) + pack
    return data, q.convert('RGB')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--collection', required=True, help='retro/ folder of a game collection in RetroStation format')
    ap.add_argument('--rdb', default=None)
    ap.add_argument('--preview', default=None, help='saves a sheet with all the generated covers')
    ap.add_argument('--download', action='store_true', help='fetches from libretro-thumbnails the covers the collection lacks')
    ap.add_argument('--fixes', default=os.path.join(ROOT, 'cache', 'cover-fixes.json'),
                    help='optional JSON file of manual corrections (default cache/cover-fixes.json)')
    a = ap.parse_args()
    rdbdir = a.rdb or (glob.glob(os.path.expanduser(
        '~/.local/share/flatpak/app/org.libretro.RetroArch/*/stable/active/files/share/libretro/database/rdb')) + [''])[0]
    print(f'Collection: {a.collection}\nrdb: {rdbdir}')
    games = json.load(open(os.path.join(a.collection, 'cache/games.json')))
    by_file = {}  # file name -> all records with that name (some repeat across systems)
    for g in games:
        for f in g.get('files', []):
            p = f.get('gameRelativePath') or f.get('gameRelativePathOrFolder') or ''
            by_file.setdefault(os.path.basename(p), []).append(g)
    by_title = {}  # (system, normalized title) -> collection records of that system
    for g in games:
        for sys_, pref in FOLDERS.items():
            if g['folder'].startswith(pref):
                names = {title_key(g.get('name', ''))}
                for f in g.get('files', []):
                    names.add(title_key(os.path.basename(f.get('gameRelativePath') or f.get('gameRelativePathOrFolder') or '')))
                for k in names:
                    if k: by_title.setdefault((sys_, k), []).append(g)
    fixes = json.load(open(a.fixes)) if os.path.exists(a.fixes) else {}
    previews, total, bytes_total = [], 0, 0
    for system, (bases, header) in SYSTEMS.items():
        folder = os.path.join(ROOT, 'roms', system)
        roms = sorted(f for f in os.listdir(folder) if not f.startswith('.')) if os.path.isdir(folder) else []
        if not roms:
            continue
        print(f'{system}: {len(roms)} games')
        by_crc, by_name, by_base = load_rdb(rdbdir, bases) if rdbdir else ({}, {}, {})
        meta = os.path.join(folder, '.meta')
        os.makedirs(meta, exist_ok=True)
        for rom in roms:
            base = os.path.splitext(rom)[0]
            # prefers the record from the system folder and with an image
            candidates = by_file.get(rom, [])
            if not any(x['folder'].startswith(FOLDERS[system]) for x in candidates):
                # copy renamed to the catalog name: look it up by title in the system folder
                candidates = by_title.get((system, title_key(rom)), [])
            g = max(candidates, key=lambda x: (x['folder'].startswith(FOLDERS[system]), bool(x.get('image')),
                                               bool(x.get('description'))), default={})
            n, m = rdb_details(os.path.join(folder, rom), header, by_crc, by_name, by_base)
            desc = (g.get('description') or '').strip()
            c = fixes.get(rom, {})
            desc = c.get('description', desc)
            year = c.get('year', m.get('releaseyear'))
            if not year:
                r = re.search(r'\b(19[789]\d|20[0-2]\d)\b', desc)
                year = int(r.group(1)) if r else ''
            genre = GENRES.get(m.get('genre', ''), m.get('genre', ''))
            dev = m.get('developer', '') or m.get('publisher', '')
            with open(os.path.join(meta, base + '.txt'), 'w', encoding='utf-8') as f:
                f.write(f'{year}\n{genre}\n{dev}\n{" ".join(desc.split())}\n')
            img = os.path.join(a.collection, 'assets/games', c.get('image') or g.get('image', '')) \
                if (c.get('image') or g.get('image')) else ''
            if not img and g.get('searchNameOptimized'):
                # no image in the record: looks for <optimized name>[digits].webp/jpg in the system folder
                img_dir = os.path.join(a.collection, 'assets/games', g['folder'])
                rx = re.compile(re.escape(g['searchNameOptimized']) + r'\d*\.(webp|jpg|png)$')
                matches = sorted(f for f in (os.listdir(img_dir) if os.path.isdir(img_dir) else []) if rx.match(f))
                img = os.path.join(img_dir, matches[0]) if matches else ''
            size = 0
            if c.get('no_cover'):
                img = ''
                old_cover = os.path.join(meta, base + '.cap')
                if os.path.exists(old_cover):  # no rm: the wrong cover goes to trash/
                    os.makedirs(os.path.join(ROOT, 'trash', 'covers'), exist_ok=True)
                    os.replace(old_cover, os.path.join(ROOT, 'trash', 'covers', system + '-' + base + '.cap'))
            if c.get('source') == 'libretro':
                img = ''  # wrong collection cover: use the libretro one
            if (not img or not os.path.exists(img)) and not c.get('no_cover') and (n or c.get('cover_name')):
                img = download_cover(m.get('_base', bases[0]), c.get('cover_name', n), network=a.download)  # without --download, cache only
            if img and os.path.exists(img):
                try:
                    data, preview = make_cover(img)
                except Exception as e:
                    print(f'    unreadable image ({e}): {img}')
                    img = ''
            if img and os.path.exists(img):
                open(os.path.join(meta, base + '.cap'), 'wb').write(data)
                size = len(data); bytes_total += size; previews.append(preview)
            total += 1
            print(f'  {base[:40]:40} {year!s:5} {genre[:14]:14} {"cover %d B" % size if size else "NO COVER"}'
                  f'{"" if desc else "  NO DESCRIPTION"}{"" if n else "  (not in libretro database)"}')
    print(f'{total} game details, {len(previews)} covers, {bytes_total} cover bytes in total')
    if a.preview and previews:
        W = Image.new('RGB', (len(previews) * (COVER_W + 4), COVER_H), 'white')
        for i, p in enumerate(previews):
            W.paste(p, (i * (COVER_W + 4), 0))
        W.resize((W.width * 2, W.height * 2), Image.NEAREST).save(a.preview)


if __name__ == '__main__':
    main()
