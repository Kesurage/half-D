#!/usr/bin/env python3
"""Generate assets/particles/raindrop.dds — uncompressed BGRA replacement.

The itch package ships gfx/particles/raindrop.dds as BC7 SRGB. The Mali
driver behind gl4es exposes no BPTC (device log: "bptc no"), so the texture
upload fails and the rain particles render with the missing-texture
checker. This generates an equivalent rain streak as an uncompressed
32-bit DDS, which every GL2/GLES path can upload. CI packs it into the
libs zip; the VFS search order puts gamelibs/diffusion first, so it
overrides the game-data file.
"""
import struct, math, os

W, H = 32, 128

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
    # dwPitchOrLinearSize — the engine's Image_DXTCalcSize uses this field as
    # the total buffer size when DDSD_LINEARSIZE is set. v0.4.1 shipped W*4
    # (a pitch) under the DDSD_PITCH flag: the loader then allocated a
    # 128-BYTE buffer for a 16384-byte texture and heap corruption followed
    # (trashed sentinel at img_dds.c:420, then system-wide crashes in
    # libft2/hwui/RefBase). Must be the full pixel size.
    h += struct.pack('<I', W * H * 4)      # dwLinearSize
    h += struct.pack('<I', 0)              # dwDepth
    h += struct.pack('<I', 1)              # dwMipMapCount
    h += b'\x00' * 44                      # dwReserved1[11]
    # ddspf
    h += struct.pack('<I', 32)             # dwSize
    h += struct.pack('<I', DDPF_ALPHAPIXELS | DDPF_RGB)
    h += struct.pack('<I', 0)              # dwFourCC (uncompressed)
    h += struct.pack('<I', 32)             # dwRGBBitCount
    h += struct.pack('<I', 0x00FF0000)     # R mask
    h += struct.pack('<I', 0x0000FF00)     # G mask
    h += struct.pack('<I', 0x000000FF)     # B mask
    h += struct.pack('<I', 0xFF000000)     # A mask
    # caps
    h += struct.pack('<I', DDSCAPS_TEXTURE)
    h += struct.pack('<I', 0)              # dwCaps2
    h += struct.pack('<I', 0)              # dwCaps3
    h += struct.pack('<I', 0)              # dwCaps4
    h += struct.pack('<I', 0)              # dwReserved2
    assert len(h) == 4 + 124, len(h)
    return h

def make_pixels():
    px = bytearray()
    cx = (W - 1) / 2.0
    for y in range(H):
        # vertical taper: soft rounded tip at top, longer fade at bottom
        ty = y / (H - 1.0)
        tip_top = min(1.0, ty / 0.12)                 # head forms in first 12%
        tip_bot = min(1.0, (1.0 - ty) / 0.30)         # tail fades over last 30%
        taper = tip_top * tip_bot
        for x in range(W):
            dx = (x - cx) / 3.2                       # horizontal gaussian core
            core = math.exp(-dx * dx)
            # the drop head (top) is a bit wider and brighter
            head = 1.0 + 0.35 * max(0.0, (0.25 - ty) / 0.25)
            a = core * taper * head
            a = max(0.0, min(1.0, a))
            alpha = int(235 * a)
            # bluish-white, slightly brighter core
            bright = 0.75 + 0.25 * core
            r = int(215 * bright)
            g = int(228 * bright)
            b = int(245 * bright)
            px += bytes((b, g, r, alpha))             # DDS memory is BGRA
    return bytes(px)

out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'particles')
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, 'raindrop.dds')
with open(out_path, 'wb') as f:
    f.write(make_header())
    f.write(make_pixels())
print("wrote", out_path, os.path.getsize(out_path), "bytes")
