## v0.4.2 — the v0.4.1 crash was my own bug, found and fixed

**Straight answer first: v0.4.1's crash was caused by my own rain-texture file, not your device and not the options menu.** Your crash dump contained the smoking gun:

```
Mem_FreeBlockBig: trashed header sentinel 2
(alloc at engine/common/imagelib/img_dds.c:420 ...)
```

### What happened
The replacement rain texture I shipped in v0.4.1 (to fix the BC7 texture your GPU can't decode) had a malformed DDS header: it declared the **per-row pitch (128 bytes)** where the loader expects the **total buffer size (16384 bytes)**. The engine therefore allocated a 128-byte buffer for a 16 KB texture, and the texture upload wrote straight through it — **heap corruption**. The actual crash backtraces pointed into Android system libraries (text rendering in `libhwui`/`libft2`, `RefBase::incStrong`) because that's where the corrupted heap happened to detonate — 5 seconds after launch, before the menu even appeared.

### What's fixed
- **The DDS file**: now declares the correct total size; the loader's own size verification passes exactly. I verified the fix by replicating the loader's allocation logic byte-for-byte against the new header.
- **The loader itself is hardened**: an uncompressed DDS that declares a per-row pitch is now sized as pitch × height, so no file of this shape can corrupt the heap again — including the broken v0.4.1 one, if it were still on disk.

### Everything from v0.4.1 is still in (it never got to run)
The crash happened during texture init, before any of v0.4.1's actual fixes executed: the hard gate on all 11 screen-capture effects (the options-menu crash fix), the shader failure negative-cache and logging, the precompile filter (365 impossible shadow permutations skipped), the load-phase timing, and the perf heartbeat with draw-call counts.

**Install:** APK **and** extract the new libs zip over the game folder (it contains the corrected raindrop.dds — make sure it overwrites the v0.4.1 one).

**Test:** same as last time — let the shader load finish, open Options and click graphics options (that's the still-unvalidated crash trigger), then send `engine.log`.
