"""ITB Birds: turns the hand-made uncompressed normal maps (and the robin colour map) into mip-mapped DXT5 DDS, using only Pillow.

  python Birds/make_normals.py            reads SRC (the originals, never changed), writes DST

- Normal maps: every mip level is averaged as a VECTOR and renormalised, so the relief does not fade out with distance.
  The alpha channel of a Skyrim normal map is the specular mask; the originals had a flat 255 (full specular everywhere),
  which is glossy for feathers. SPEC_ALPHA below is the one value to change.
- Colour map (robin): same compression and mip chain, picture unchanged.
"""
import io
import os
import struct
import sys

import numpy as np
from PIL import Image

SRC = 'E:/WorkSpace/ITB-backups/user_textures_20261120/'          # the originals as the author made them
DST = 'E:/WorkSpace/ITB/Birds/assets/textures/itb/fauna/birds/'
SPEC_ALPHA = 96                                                  # 0-255; the originals had 255
SPECIES = ('pigeon', 'dove', 'sparrow', 'crow', 'starling', 'robin')


def dxt5(img):
    w, h = img.size
    if w < 4 or h < 4:
        img = img.resize((max(4, w), max(4, h)), Image.BILINEAR)
    buf = io.BytesIO()
    img.save(buf, format='DDS', pixel_format='DXT5')
    return buf.getvalue()[128:]


def write_dds(levels, out):
    """levels: list of RGBA PIL images from the full size down to 1x1."""
    first = io.BytesIO()
    levels[0].save(first, format='DDS', pixel_format='DXT5')
    hdr = bytearray(first.getvalue()[:128])
    flags = struct.unpack_from('<I', hdr, 8)[0] | 0x20000          # DDSD_MIPMAPCOUNT
    struct.pack_into('<I', hdr, 8, flags)
    struct.pack_into('<I', hdr, 28, len(levels))
    caps = struct.unpack_from('<I', hdr, 108)[0] | 0x401008          # COMPLEX | MIPMAP | TEXTURE
    struct.pack_into('<I', hdr, 108, caps)
    open(out, 'wb').write(bytes(hdr) + b''.join(dxt5(l) for l in levels))


def normalize(v):
    n = np.sqrt((v ** 2).sum(axis=-1, keepdims=True))
    return v / np.maximum(n, 1e-6)


def normal_levels(path, spec_alpha):
    a = np.asarray(Image.open(path).convert('RGBA'), np.float32) / 255.0
    vec = normalize(a[..., :3] * 2.0 - 1.0)
    levels = []
    while True:
        h, w = vec.shape[:2]
        rgb = (np.clip(vec * 0.5 + 0.5, 0, 1) * 255.0 + 0.5).astype(np.uint8)
        al = np.full((h, w, 1), spec_alpha, np.uint8)
        levels.append(Image.fromarray(np.concatenate([rgb, al], axis=-1), 'RGBA'))
        if w == 1 and h == 1:
            break
        nh, nw = max(1, h // 2), max(1, w // 2)
        # average blocks of the vectors (the image sizes are powers of two)
        v = vec[:nh * (h // nh), :nw * (w // nw)]
        v = v.reshape(nh, h // nh, nw, w // nw, 3).mean(axis=(1, 3))
        vec = normalize(v)
    return levels


def colour_levels(path):
    im = Image.open(path).convert('RGBA')
    levels = [im]
    w, h = im.size
    while not (w == 1 and h == 1):
        w, h = max(1, w // 2), max(1, h // 2)
        levels.append(im.resize((w, h), Image.LANCZOS))
    return levels


if __name__ == '__main__':
    os.makedirs(DST, exist_ok=True)
    for sp in SPECIES:
        n = os.path.join(SRC, f'itb_bird_{sp}_n.dds')
        lv = normal_levels(n, SPEC_ALPHA)
        write_dds(lv, os.path.join(DST, f'itb_bird_{sp}_n.dds'))
        print(sp, '_n', len(lv), 'levels,', os.path.getsize(os.path.join(DST, f'itb_bird_{sp}_n.dds')), 'bytes')
    lv = colour_levels(os.path.join(SRC, 'itb_bird_robin.dds'))
    write_dds(lv, os.path.join(DST, 'itb_bird_robin.dds'))
    print('robin colour', len(lv), 'levels,', os.path.getsize(os.path.join(DST, 'itb_bird_robin.dds')), 'bytes')
    # the painted robin proposal, next to the author's: a separate name, not used by any mesh until it is swapped in
    prop = 'E:/WorkSpace/ITB/Birds/work/itb_bird_robin_proposal.png'
    if os.path.exists(prop):
        write_dds(colour_levels(prop), os.path.join(DST, 'itb_bird_robin_painted.dds'))
        print('robin painted proposal written')
