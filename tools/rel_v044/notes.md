## v0.4.4 — the crash was in my v0.4.0 "shader cache", not in gl4es. Reverted.

**Good news first: v0.4.3 worked.** Your crash dump shows the game booting, mounting everything, loading the map and starting a session — the filesystem/lifecycle fixes all held. This crash happened *in the 3D world* (the menu map), which is why it looked like "opening any menu kills it": the game's menu is a live 3D scene.

### What actually crashed
```
SEGV at 0x20 in fpe_CustomShader (gl4es fpe.c:468) — R_DrawWorld → glDrawElements
```

I compared everything against the reference port (yalnie/Diffusion-Android) before touching anything, and the result was humbling: **my engine and my gl4es were already identical to theirs.** The one thing that differed was a change I made in v0.4.0: I enabled the **shader binary disk cache** on gl4es (the game normally keeps it off there). Programs restored from that cache have no shader *source* attached — just precompiled blobs — and when the GL state changed (fog/alpha while the menu world draws), gl4es tried to re-generate a customized variant of that program, read its source… and there was none. Crash. The same source-less programs are also the likely cause of **fog/alpha visual glitches and artifacts** you saw.

v0.4.0 did that to speed up loading. It wasn't worth it. It's reverted: on Android every shader now compiles from source — exactly like the reference port. (First minutes after a map change may hitch briefly while shaders compile on first use, then they're cached in memory for the session — same as the reference.)

### Also in this build
- **gl4es hardening**: even if a source-less program ever gets bound again, gl4es now falls back to the plain program instead of crashing (upstream gl4es still lacks this guard).
- **Debris particles fixed**: your log showed 4 particle textures missing from the 1.21 game data (`fleck_wood1`, `fleck_dirt`, `glass_gib`, `tile_gib` — wood/dirt/glass/tile impact debris). They now ship as neutral placeholder chips so debris renders instead of missing-texture checkers.
- A list of missing **sound files** (fvox beep, some footsteps/pain voicelines) is from the 1.21 data package itself — the game continues without them; nothing I can ship fixes that short of a newer game data version.

### Install
1. Install the APK over the old one.
2. Extract the libs zip over the game folder (adds the particle textures; also carries the corrected rain texture).

### Test — in this order
1. Launch, let the menu load, **open Options → graphics** (the old trigger — it should no longer freeze or reboot the device).
2. Start a game, play a few minutes: watch for the crash point that used to happen, and note whether **texture glitches / lighting / character artifacts** are gone (the reverted shader cache is expected to fix those too).
3. If anything still misbehaves: the APK also ships a second renderer. In the launcher's command-line arguments (or the in-game console via `ref_dll gles3compat` then `vid_restart`) you can try **`-ref gles3compat`** — a native GLES 3.0 path instead of gl4es. Send the engine.log either way; with v0.4.3's append-mode logging it tells the full story now.
