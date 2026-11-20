"""PNG -> mip-mapped DXT5 DDS using only Pillow (no external tools). python Birds/make_dds.py"""
import io, os, struct, sys
from PIL import Image

def to_dds(png, out):
    im = Image.open(png).convert('RGBA')
    w, h = im.size
    levels = []
    cur = im
    cw, ch = w, h
    while True:
        pad = cur if (cw >= 4 and ch >= 4) else cur.resize((max(4, cw), max(4, ch)), Image.BILINEAR)
        buf = io.BytesIO(); pad.save(buf, format='DDS', pixel_format='DXT5'); raw = buf.getvalue()
        levels.append(raw[128:])
        if cw == 1 and ch == 1:
            break
        cw, ch = max(1, cw // 2), max(1, ch // 2)
        cur = im.resize((cw, ch), Image.LANCZOS)
    first = io.BytesIO(); im.save(first, format='DDS', pixel_format='DXT5')
    hdr = bytearray(first.getvalue()[:128])
    flags = struct.unpack_from('<I', hdr, 8)[0] | 0x20000          # DDSD_MIPMAPCOUNT
    struct.pack_into('<I', hdr, 8, flags)
    struct.pack_into('<I', hdr, 28, len(levels))
    caps = struct.unpack_from('<I', hdr, 108)[0] | 0x401008          # COMPLEX | MIPMAP | TEXTURE
    struct.pack_into('<I', hdr, 108, caps)
    open(out, 'wb').write(bytes(hdr) + b''.join(levels))
    return len(levels)

if __name__ == '__main__':
    work = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'work')
    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assets', 'textures', 'itb', 'fauna', 'birds')
    for sp in ('pigeon', 'dove', 'sparrow', 'crow', 'starling'):
        n = to_dds(os.path.join(work, f'itb_bird_{sp}.png'), os.path.join(dst, f'itb_bird_{sp}.dds'))
        print(sp, 'levels', n, os.path.getsize(os.path.join(dst, f'itb_bird_{sp}.dds')), 'bytes')
