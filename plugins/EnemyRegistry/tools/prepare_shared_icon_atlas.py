"""Offline RGBA8 authoring. Runtime only copies prebuilt TEX mip rows.

Use the existing REE-Lib EnemyIconBuilder for TEX containers. Never deploys.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

from PIL import Image


def read_bc7_mips(path):
    data = path.read_bytes()
    if (len(data) < 148 or data[:4] != b'DDS ' or data[84:88] != b'DX10'
            or struct.unpack_from('<I', data, 128)[0] != 98):
        raise ValueError('Expected existing BC7_UNORM base DDS')
    height, width = struct.unpack_from('<II', data, 12)
    if struct.unpack_from('<I', data, 28)[0] != 3:
        raise ValueError('Expected 3 native mip levels')
    images = []
    offset = 148
    for level in range(3):
        mip_width, mip_height = width >> level, height >> level
        size = ((mip_width + 3) // 4) * ((mip_height + 3) // 4) * 16
        header = bytearray(data[:148])
        struct.pack_into('<II', header, 12, mip_height, mip_width)
        struct.pack_into('<I', header, 20, size)
        struct.pack_into('<I', header, 28, 1)
        with Image.open(io.BytesIO(header + data[offset:offset + size])) as image:
            images.append(image.convert('RGBA'))
        offset += size
    if offset != len(data):
        raise ValueError('Unexpected DDS data')
    return images


def write_rgba_dds(path, images):
    # Standard DX10 DDS envelope; mip pixels are authored by Pillow, TEX by REE-Lib.
    width, height = images[0].size
    header = bytearray(148)
    header[:4] = b'DDS '
    struct.pack_into('<7I', header, 4, 124, 0x2100F, height, width, width * 4, 0, len(images))
    struct.pack_into('<II4s', header, 76, 32, 4, b'DX10')
    struct.pack_into('<I', header, 108, 0x401008)
    struct.pack_into('<5I', header, 128, 28, 3, 0, 1, 0)
    for level, image in enumerate(images):
        if image.size != (width >> level, height >> level):
            raise ValueError('Invalid mip dimensions')
    path.write_bytes(header + b''.join(image.convert('RGBA').tobytes() for image in images))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--converter', type=Path, required=True)
    parser.add_argument('--base', type=Path, help='Framework maintainer only: current BC7 base TEX')
    parser.add_argument('--output', type=Path, required=True, help='Staging root, never a game deployment path')
    parser.add_argument('--icon', action='append', default=[], help='FIXED_ID=authored.png')
    args = parser.parse_args()
    if args.base is None and not args.icon:
        parser.error('provide --icon or --base')
    root = args.output.resolve()
    manifest_path = root / 'manifest.json'
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    else:
        manifest = {'format': 'EnemyRegistry.Package/v3', 'files': [], 'retiredShared': []}
    if manifest.get('format') != 'EnemyRegistry.Package/v3':
        raise ValueError('Expected EnemyRegistry.Package/v3 manifest')
    files = {entry['path'].casefold(): entry for entry in manifest['files']}
    if len(files) != len(manifest['files']):
        raise ValueError('Duplicate package manifest path')
    icons = {}
    for entry in args.icon:
        fixed, path = entry.split('=', 1)
        fixed = int(fixed)
        if fixed in icons or not -(2 ** 31) <= fixed < 2 ** 31 or fixed in (-1, 0):
            raise ValueError('Unique non-reserved Int32 fixed IDs required')
        declaration = root / f'reframework/data/EnemyRegistry/{fixed}.enemy.json'
        enemy = json.loads(declaration.read_text(encoding='utf-8-sig'))
        if enemy.get('format') != 'EnemyRegistry.Enemy/v4' or enemy.get('fixedId') != fixed:
            raise ValueError(f'Enemy declaration format/fixedId mismatch: {declaration}')
        with Image.open(path) as source:
            icons[fixed] = (enemy, source.convert('RGBA'))
    configured_ids = set(icons)
    for declaration in (root / 'reframework/data/EnemyRegistry').glob('*.enemy.json'):
        enemy = json.loads(declaration.read_text(encoding='utf-8-sig'))
        if isinstance(enemy, dict) and enemy.get('icon', {}).get('texture'):
            configured_ids.add(enemy['fixedId'])
    if len(configured_ids) > 63:
        raise ValueError('Shared enemy atlas slot capacity exceeded (63 icons)')
    work = root.parent / (root.name + '-authoring')
    work.mkdir(parents=True, exist_ok=True)
    changed_files = {}
    if args.base is not None:
        subprocess.run([str(args.converter), 'decode', str(args.base), str(work / 'base.dds')], check=True)
        mips = read_bc7_mips(work / 'base.dds')
        if mips[0].size != (2048, 2048):
            raise ValueError('Expected 2048 square atlas')
        base = 'reframework/data/EnemyRegistry/atlas/base.tex.241106027'
        write_rgba_dds(work / 'base-rgba.dds', mips)
        subprocess.run([str(args.converter), 'encode', str(work / 'base-rgba.dds'), str(root / base)], check=True)
        changed_files[base] = 'owned-resource'
        mips[0].save(work / 'base-preview.png')
    for fixed, (enemy, image) in icons.items():
        levels = [image.resize((100 >> i, 100 >> i), Image.Resampling.LANCZOS) for i in range(3)]
        dds = work / f'{fixed}.dds'
        write_rgba_dds(dds, levels)
        uri = f'gui/custom/enemyregistry/tiles/{fixed}.tex'
        tile = 'natives/stm/' + uri + '.241106027'
        subprocess.run([str(args.converter), 'encode', str(dds), str(root / tile)], check=True)
        declaration = f'reframework/data/EnemyRegistry/{fixed}.enemy.json'
        enemy['icon'] = {'texture': uri}
        (root / declaration).write_text(json.dumps(enemy, ensure_ascii=False, indent=2), encoding='utf-8')
        changed_files.update({tile: 'owned-resource', declaration: 'registration'})
    for relative, mode in changed_files.items():
        data = (root / relative).read_bytes()
        files[relative.casefold()] = {
            'path': relative, 'mode': mode, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()
        }
    manifest['files'] = sorted(files.values(), key=lambda entry: entry['path'])
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'files': len(manifest['files']), 'root': str(root)}))


if __name__ == '__main__':
    main()
