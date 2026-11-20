"""ITB Birds: paints the bird textures for Bethesda's SmallBird01 mesh (its UV layout is kept; only the picture changes).

The mesh UVs were made for a hawk wing atlas, so a plain recolour looks wrong. Here every UV island is rasterized with
per-pixel information from the 3D mesh (is it belly or back, how far along the body, how far out along the wing, which bone),
and bird markings are painted from that. Own work, no pixels are taken from any texture.

  python Birds/birdpaint.py            writes PNGs and previews to Birds/work/
"""
import os, sys, math
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SIZE_W, SIZE_H = 512, 1024
sys.path.insert(0, 'C:/Skyrim-Claude Code Modder\'s Toolkit/tools/pynifly/io_scene_nifly')


def load_mesh(path):
    from pyn import pynifly
    nf = pynifly.NifFile(path)
    s = nf.shapes[0]
    bw = {}
    for b, ws in s.bone_weights.items():
        for v, w in ws:
            bw.setdefault(v, {})[b] = w
    dom = {v: max(d, key=d.get) for v, d in bw.items()}
    return dict(V=np.array(s.verts, np.float32), UV=np.array(s.uvs, np.float32), N=np.array(s.normals, np.float32),
                T=np.array(s.tris, np.int32), dom=dom)


BONES = ['Bone01', 'Bone02', 'Bone03', 'Bone04', 'Bone05']


def rasterize(m):
    """Attribute maps over the UV rectangle: mask, bone index, nz, nx, y, |x|, z, and a wing-span coordinate."""
    W, H = SIZE_W, SIZE_H
    A = {k: np.zeros((H, W), np.float32) for k in ('mask', 'bone', 'nz', 'nx', 'y', 'ax', 'z')}
    V, UV, N, T, dom = m['V'], m['UV'], m['N'], m['T'], m['dom']
    for t in T:
        p = np.array([[UV[i][0] * W, UV[i][1] * H] for i in t])
        x0, x1 = int(max(0, math.floor(p[:, 0].min()))), int(min(W - 1, math.ceil(p[:, 0].max())))
        y0, y1 = int(max(0, math.floor(p[:, 1].min()))), int(min(H - 1, math.ceil(p[:, 1].max())))
        if x1 <= x0 or y1 <= y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (p[1, 1] - p[2, 1]) * (p[0, 0] - p[2, 0]) + (p[2, 0] - p[1, 0]) * (p[0, 1] - p[2, 1])
        if abs(d) < 1e-9:
            continue
        w0 = ((p[1, 1] - p[2, 1]) * (xs - p[2, 0]) + (p[2, 0] - p[1, 0]) * (ys - p[2, 1])) / d
        w1 = ((p[2, 1] - p[0, 1]) * (xs - p[2, 0]) + (p[0, 0] - p[2, 0]) * (ys - p[2, 1])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= -0.002) & (w1 >= -0.002) & (w2 >= -0.002)
        if not inside.any():
            continue
        def mix(vals):
            return w0 * vals[0] + w1 * vals[1] + w2 * vals[2]
        nz = mix([N[i][2] for i in t]); nx = mix([N[i][0] for i in t]); yy = mix([V[i][1] for i in t])
        axx = mix([abs(V[i][0]) for i in t]); zz = mix([V[i][2] for i in t])
        bone = BONES.index(dom.get(int(t[0]), 'Bone01'))
        sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        for name, val in (('nz', nz), ('nx', nx), ('y', yy), ('ax', axx), ('z', zz)):
            A[name][sl][inside] = val[inside]
        A['bone'][sl][inside] = bone
        A['mask'][sl][inside] = 1.0
    return A


def noise(h, w, cell, seed):
    r = np.random.RandomState(seed)
    small = r.rand(max(2, h // cell + 2), max(2, w // cell + 2)).astype(np.float32)
    im = Image.fromarray((small * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
    return np.asarray(im, np.float32) / 255.0


def stripes(A, freq, seed, wob=0.35):
    """Fine feather lines running from the wing root outwards (along |x|) with some wobble."""
    n = noise(SIZE_H, SIZE_W, 24, seed)
    return 0.5 + 0.5 * np.sin((A['y'] * freq) + n * 6.0 * wob)


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def col(*c):
    return np.array(c, np.float32)


def blend(base, color, w):
    return base * (1 - w[..., None]) + color * w[..., None]


def regions(A):
    bone = A['bone']
    head = (bone == 3)
    wing = (bone == 2) | (bone == 4)
    aux = (bone == 1)
    body = (bone == 0)
    return head, wing, aux, body


def paint(species, A):
    H, W = SIZE_H, SIZE_W
    head, wing, aux, body = regions(A)
    nz, nx, y, ax = A['nz'], A['nx'], A['y'], A['ax']
    belly = smooth(-nz, -0.05, 0.45)           # facing down
    back = smooth(nz, 0.15, 0.6)               # facing up
    n1 = noise(H, W, 6, 1); n2 = noise(H, W, 16, 2); n3 = noise(H, W, 40, 3)
    st = stripes(A, 1.6, 4)
    img = np.zeros((H, W, 3), np.float32)
    front = smooth(y, 4.0, 10.0)               # neck and head end
    tail = smooth(-y, 9.0, 15.0)               # tail end
    tip = smooth(ax, 15.0, 21.0)               # wing tip

    if species == 'pigeon':
        base = col(0.50, 0.54, 0.61)
        img[:] = base
        img = blend(img, col(0.66, 0.69, 0.74), belly * 0.8)
        img = blend(img, col(0.40, 0.44, 0.51), back * 0.55)
        # glossy green and purple neck patch
        neck = smooth(y, 2.0, 5.0) * (1 - smooth(y, 8.0, 10.5)) * (body | head) * (0.45 + 0.35 * belly)
        sheen = 0.5 + 0.5 * np.sin(nx * 2.2 + n3 * 2.5)
        img = blend(img, col(0.22, 0.50, 0.40) * (1 - sheen[..., None]) + col(0.52, 0.28, 0.55) * sheen[..., None], neck * 0.7)
        # wings: pale grey, two black bars, dark tips, fine feather lines
        w = wing.astype(np.float32)
        img = blend(img, col(0.64, 0.68, 0.74), w)
        for yc in (-1.0, -5.0):
            bar = (1 - smooth(np.abs(y - yc), 0.35, 0.95)) * smooth(ax, 5.0, 8.0) * (1 - smooth(ax, 15.0, 18.0)) * w
            img = blend(img, col(0.07, 0.07, 0.09), bar * 0.9)
        img = blend(img, col(0.22, 0.23, 0.27), tip * w * 0.8)
        img *= (0.9 + 0.2 * st[..., None] * w[..., None] + (1 - w[..., None]) * 0.1)
        # tail: dark with a black end band
        tl = tail * (body | aux)
        img = blend(img, col(0.36, 0.39, 0.45), tl * 0.9)
        img = blend(img, col(0.06, 0.06, 0.08), smooth(-y, 15.5, 17.5) * (body | aux))
        # head and beak
        img = blend(img, col(0.44, 0.48, 0.55), head.astype(np.float32) * 0.6)
        beak = smooth(y, 12.6, 13.3) * head
        img = blend(img, col(0.17, 0.16, 0.16), beak.astype(np.float32))
    elif species == 'dove':
        img[:] = col(0.93, 0.92, 0.89)
        img = blend(img, col(0.82, 0.81, 0.79), belly * 0.7 + back * 0.15)
        w = wing.astype(np.float32)
        img = blend(img, col(0.86, 0.85, 0.82), w * 0.8)
        img = blend(img, col(0.62, 0.60, 0.58), tip * w * 0.7)
        img *= (0.93 + 0.14 * st[..., None] * w[..., None] + (1 - w[..., None]) * 0.05)
        img = blend(img, col(0.80, 0.78, 0.74), tail * (body | aux) * 0.8)
        beak = smooth(y, 12.6, 13.3) * head
        img = blend(img, col(0.72, 0.52, 0.50), beak.astype(np.float32))
    elif species == 'sparrow':
        img[:] = col(0.43, 0.29, 0.17)                               # brown back
        streak = (n1 > 0.58).astype(np.float32) * back
        img = blend(img, col(0.12, 0.08, 0.05), streak * 0.8)
        img = blend(img, col(0.74, 0.71, 0.65), belly * 0.95)         # pale buff belly
        # black bib on the throat and chest
        bib = smooth(y, 3.0, 5.0) * (1 - smooth(y, 9.5, 11.5)) * belly * (body | head)
        img = blend(img, col(0.08, 0.07, 0.07), bib * 0.9)
        # grey crown and cheeks
        img = blend(img, col(0.52, 0.52, 0.50), head.astype(np.float32) * back * 0.9)
        img = blend(img, col(0.80, 0.78, 0.74), head.astype(np.float32) * (1 - back) * 0.6)
        w = wing.astype(np.float32)
        img = blend(img, col(0.40, 0.26, 0.15), w)
        bar = (1 - smooth(np.abs(y - -2.0), 0.7, 1.6)) * smooth(ax, 5.0, 8.0) * (1 - smooth(ax, 16.0, 19.0)) * w
        img = blend(img, col(0.88, 0.86, 0.80), bar * 0.9)
        img = blend(img, col(0.17, 0.12, 0.08), tip * w * 0.7)
        img *= (0.88 + 0.24 * st[..., None] * w[..., None] + (1 - w[..., None]) * 0.12 * n1[..., None])
        img = blend(img, col(0.34, 0.24, 0.15), tail * (body | aux) * 0.9)
        beak = smooth(y, 12.6, 13.3) * head
        img = blend(img, col(0.16, 0.14, 0.12), beak.astype(np.float32))
    elif species == 'crow':
        base = col(0.045, 0.045, 0.06)
        img[:] = base
        sheen = np.clip(0.5 + 0.5 * nz, 0, 1) * (0.4 + 0.6 * n2)
        img = blend(img, col(0.10, 0.12, 0.20), sheen * 0.8)           # cool blue gloss on top surfaces
        w = wing.astype(np.float32)
        img *= (0.85 + 0.3 * st[..., None] * w[..., None] + (1 - w[..., None]) * 0.15)
        img = blend(img, col(0.02, 0.02, 0.025), tip * w * 0.6)
        beak = smooth(y, 12.6, 13.3) * head
        img = blend(img, col(0.03, 0.03, 0.03), beak.astype(np.float32))
    elif species == 'starling':
        img[:] = col(0.07, 0.07, 0.09)
        gloss = 0.5 + 0.5 * np.sin(nx * 2.5 + n3 * 2.5)
        img = blend(img, col(0.10, 0.30, 0.22) * (1 - gloss[..., None]) + col(0.34, 0.16, 0.42) * gloss[..., None], 0.55 * np.clip(0.4 + 0.6 * nz, 0, 1))
        # pale speckles
        speck = (noise(H, W, 3, 11) > 0.87).astype(np.float32) * (body | head | aux)
        img = blend(img, col(0.80, 0.76, 0.64), speck * 0.7)
        w = wing.astype(np.float32)
        img = blend(img, col(0.16, 0.12, 0.10), w * 0.7)
        edge = (stripes(A, 1.6, 9) > 0.8).astype(np.float32) * w
        img = blend(img, col(0.55, 0.42, 0.28), edge * 0.6)
        beak = smooth(y, 12.4, 13.2) * head
        img = blend(img, col(0.86, 0.72, 0.12), beak.astype(np.float32))
    elif species == 'robin':
        # European robin: olive-brown upperparts, orange-red face and breast, cream belly, dark thin beak
        img[:] = col(0.36, 0.29, 0.17)
        img = blend(img, col(0.45, 0.36, 0.21), n2 * back * 0.45)              # soft mottling on the back
        img = blend(img, col(0.91, 0.88, 0.79), belly * 0.95)                  # cream belly
        breast = smooth(y, 3.0, 5.0) * (1 - smooth(y, 9.5, 11.5)) * belly * (body | head)
        img = blend(img, col(0.85, 0.33, 0.09), breast * 0.95)                 # orange-red breast
        face = head.astype(np.float32) * (1 - back * 0.85)                     # orange-red face and throat
        img = blend(img, col(0.85, 0.35, 0.10), face * 0.92)
        w = wing.astype(np.float32)
        img = blend(img, col(0.33, 0.26, 0.16), w)
        edge = (stripes(A, 1.6, 7) > 0.78).astype(np.float32) * w              # pale feather edges
        img = blend(img, col(0.56, 0.45, 0.29), edge * 0.45)
        img = blend(img, col(0.20, 0.15, 0.10), tip * w * 0.7)
        img *= (0.9 + 0.2 * st[..., None] * w[..., None] + (1 - w[..., None]) * 0.1 * n1[..., None])
        img = blend(img, col(0.30, 0.23, 0.15), tail * (body | aux) * 0.9)
        beak = smooth(y, 12.6, 13.3) * head
        img = blend(img, col(0.13, 0.11, 0.09), beak.astype(np.float32))
    else:
        raise ValueError(species)
    img = add_eye(img, species, A)
    return np.clip(img, 0, 1)


EYE_UV = (0.199, 0.158)   # a side face of the head, about a third of the way back from the beak; both sides share these UVs
EYE_R = 7.0


def add_eye(img, species, A):
    H, W = img.shape[:2]
    cx, cy = EYE_UV[0] * W, EYE_UV[1] * H
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    d = np.sqrt((xx + 0.5 - cx) ** 2 + ((yy + 0.5 - cy) * 0.5) ** 2 * 4.0 / 4.0)
    head = (A['bone'] == 3)
    iris = {'pigeon': (0.85, 0.42, 0.08), 'dove': (0.45, 0.10, 0.10), 'sparrow': (0.20, 0.12, 0.06),
            'crow': (0.30, 0.22, 0.14), 'starling': (0.22, 0.14, 0.08), 'robin': (0.10, 0.06, 0.04)}[species]
    ring = (d < EYE_R) & head
    pupil = (d < EYE_R * 0.55) & head
    spark = (np.sqrt((xx + 0.5 - (cx - 1.5)) ** 2 + (yy + 0.5 - (cy - 1.5)) ** 2) < 1.2) & head
    out = img.copy()
    out[ring] = np.array(iris, np.float32)
    out[pupil] = np.array((0.02, 0.02, 0.02), np.float32)
    out[spark] = np.array((0.95, 0.95, 0.95), np.float32)
    return out


def dilate(img, mask, iters=8):
    img = img.copy(); m = mask.astype(bool)
    for _ in range(iters):
        grow = np.zeros_like(m); acc = np.zeros_like(img); cnt = np.zeros(m.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sm = np.roll(m, (dy, dx), (0, 1)); si = np.roll(img, (dy, dx), (0, 1))
            add = sm & ~m
            acc[add] += si[add]; cnt[add] += 1; grow |= add
        img[grow] = acc[grow] / cnt[grow][:, None]
        m = m | grow
    return img


def preview(m, tex, out, size=420):
    """A tiny software render: three views of the textured mesh, sampled straight from the texture."""
    V, UV, T = m['V'], m['UV'], m['T']
    H, W = tex.shape[:2]
    views = []
    for ax, ay, az, sign in ((0, 1, 2, 1), (1, 2, 0, 1), (0, 2, 1, -1)):
        canvas = np.full((size, size, 3), 0.32, np.float32); zbuf = np.full((size, size), -1e9, np.float32)
        sc = size / 56.0
        for t in T:
            pts = np.array([[size / 2 + V[i][ax] * sc, size / 2 - V[i][ay] * sc] for i in t]); depth = np.mean([V[i][az] * sign for i in t])
            x0, x1 = int(max(0, pts[:, 0].min())), int(min(size - 1, pts[:, 0].max()))
            y0, y1 = int(max(0, pts[:, 1].min())), int(min(size - 1, pts[:, 1].max()))
            if x1 <= x0 or y1 <= y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (pts[1, 1] - pts[2, 1]) * (pts[0, 0] - pts[2, 0]) + (pts[2, 0] - pts[1, 0]) * (pts[0, 1] - pts[2, 1])
            if abs(d) < 1e-9:
                continue
            w0 = ((pts[1, 1] - pts[2, 1]) * (xs - pts[2, 0]) + (pts[2, 0] - pts[1, 0]) * (ys - pts[2, 1])) / d
            w1 = ((pts[2, 1] - pts[0, 1]) * (xs - pts[2, 0]) + (pts[0, 0] - pts[2, 0]) * (ys - pts[2, 1])) / d
            w2 = 1 - w0 - w1
            ins = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            u = w0 * UV[t[0]][0] + w1 * UV[t[1]][0] + w2 * UV[t[2]][0]; v = w0 * UV[t[0]][1] + w1 * UV[t[1]][1] + w2 * UV[t[2]][1]
            tx = np.clip((u * W).astype(int), 0, W - 1); ty = np.clip((v * H).astype(int), 0, H - 1)
            sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
            upd = ins & (depth > zbuf[sl])
            zbuf[sl][upd] = depth
            c = tex[ty, tx]
            canvas[sl][upd] = c[upd]
        views.append(canvas)
    sheet = np.concatenate(views, axis=1)
    Image.fromarray((np.clip(sheet, 0, 1) * 255).astype(np.uint8)).save(out)


SPECIES = ['pigeon', 'dove', 'sparrow', 'crow', 'starling']

if __name__ == '__main__':
    work = os.path.join(HERE, 'work'); os.makedirs(work, exist_ok=True)
    mesh = sys.argv[1] if len(sys.argv) > 1 else 'C:/Users/JulioV/AppData/Local/Temp/claude/27690b45ae6800bf/e8ba8216-5aca-40b6-8829-289afdb5596b/scratchpad/vb/smallbird01.nif'
    m = load_mesh(mesh)
    A = rasterize(m)
    print('mesh', len(m['V']), 'verts; covered pixels', int(A['mask'].sum()), 'of', SIZE_W * SIZE_H)
    for sp in SPECIES:
        img = paint(sp, A)
        img = dilate(img, A['mask'])
        Image.fromarray((img * 255).astype(np.uint8)).save(os.path.join(work, f'itb_bird_{sp}.png'))
        preview(m, img, os.path.join(work, f'preview_{sp}.png'))
        print('painted', sp)
