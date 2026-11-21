#!/usr/bin/env python3
"""Generate placeholder DDS for particle textures missing from the itch 1.21
game data (device log 2026-11-22 02:04: "texture not found:
gfx/particles/fleck_wood1.dds" + 3 more).

The current client's data/particles.txt references these, but the 1.21 data
package never shipped them, so impact debris particles render as missing
textures. These are small neutral placeholders (soft irregular chips with a
per-material tint) in uncompressed 32-bit BGRA DDS — the only format every
GL2/GLES path can upload. CI packs them into the libs zip; if a future game
data update ships real textures, the VFS search order (gamelibs/diffusion
first) still lets the user's files win only when extracted over — these are
only found when the real file is absent.

Header layout is shared with tools/make_raindrop.py (LINEARSIZE = full pixel
size — see the heap-corruption postmortem there).
"""
import struct, math, os, random

W, H = 16, 16

DDSD_CAPS        = 0x1
DDSD_HEIGHT      = 0x2
DDSD_WIDTH       = 0x4
DDSD_LINEARSIZE  = 0x80000
DDSD_PIXELFORMAT = 0x1000
DDPF_ALPHAPIXELS = 0x1
DDPF_RGB         = 0x40
DDSCAPS_TEXTURE  = 0x1000

def make_header():
    h = b'DDS '
    h += struct.pack('<I', 124)            # dwSize
    h += struct.pack('<I', DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_LINEARSIZE | DDSD_PIXELFORMAT)
    h += struct.pack('<I', H)              # dwHeight
    h += struct.pack('<I', W)              # dwWidth
    h += struct.pack('<I', W * H * 4)      # dwLinearSize — full pixel size, never a pitch
    h += struct.pack('<I', 0)              # dwDepth
    h += struct.pack('<I', 1)              # dwMipMapCount
    h += b'\x00' * 44
    h += struct.pack('<I', 32)             # ddspf dwSize
    h += struct.pack('<I', DDPF_ALPHAPIXELS | DDPF_RGB)
    h += struct.pack('<I', 0)              # uncompressed
    h += struct.pack('<I', 32)             # dwRGBBitCount
    h += struct.pack('<I', 0x00FF0000)     # R
    h += struct.pack('<I', 0x0000FF00)     # G
    h += struct.pack('<I', 0x000000FF)     # B
    h += struct.pack('<I', 0xFF000000)     # A
    h += struct.pack('<I', DDSCAPS_TEXTURE)
    h += struct.pack('<I', 0) * 4
    assert len(h) == 4 + 124, len(h)
    return h

def chip_pixels(base_rgb, seed, coverage=0.62):
    """An irregular soft-edged chip: union of a few offset ellipses with a
    noisy boundary, alpha-faded. Deterministic per seed."""
    rng = random.Random(seed)
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0
    # 3-4 overlapping ellipses = irregular silhouette
    ells = []
    for _ in range(rng.randint(3, 4)):
        ells.append((
            cx + rng.uniform(-2.2, 2.2),           # ox
            cy + rng.uniform(-2.2, 2.2),           # oy
            rng.uniform(2.8, 4.6),                 # rx
            rng.uniform(2.8, 4.6),                 # ry
            rng.uniform(0.0, math.pi),             # rotation
        ))
    px = bytearray()
    for y in range(H):
        for x in range(W):
            inside = 0.0
            for ox, oy, rx, ry, rot in ells:
                dx, dy = x - ox, y - oy
                c, s = math.cos(rot), math.sin(rot)
                lx = (dx * c + dy * s) / rx
                ly = (-dx * s + dy * c) / ry
                d = lx * lx + ly * ly
                if d < 1.0:
                    inside = max(inside, 1.0 - d)  # soft falloff toward edge
            a = max(0.0, min(1.0, inside / max(1e-6, coverage)))
            # slight interior shading noise so it doesn't look like a flat disc
            n = rng.uniform(0.82, 1.0)
            r = int(base_rgb[0] * n)
            g = int(base_rgb[1] * n)
            b = int(base_rgb[2] * n)
            px += bytes((b, g, r, int(255 * a)))
    return bytes(px)

# name -> (base RGB, seed). Tints follow the material each texture stands for.
TEXTURES = {
    'fleck_wood1.dds': ((150, 106, 60), 11),   # wood splinter, warm brown
    'fleck_dirt.dds':  ((110, 92, 70),  23),   # dirt clod, dull brown-gray
    'glass_gib.dds':   ((198, 224, 228), 37),  # glass shard, pale cyan-white
    'tile_gib.dds':    ((196, 196, 188), 53),  # tile chip, light gray
}

out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'particles')
os.makedirs(out_dir, exist_ok=True)
for name, (rgb, seed) in TEXTURES.items():
    path = os.path.join(out_dir, name)
    with open(path, 'wb') as f:
        f.write(make_header())
        f.write(chip_pixels(rgb, seed))
    print("wrote", path, os.path.getsize(path), "bytes")
