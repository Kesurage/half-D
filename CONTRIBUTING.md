# Contributing to half-D

Thanks for considering a contribution. This is a one-device port (see `docs/DEVICE_NOTES.md`),
so every change gets validated against real device logs — the workflow below is what
keeps that loop fast.

## Build

Requirements: Android NDK + SDK, Java 17, Python 3, `wget`. `tools/check_build_env.sh`
verifies the environment.

```sh
# everything is driven from the repo root; the engine project uses waf
cd engine
./waf configure -T release --android=arm64-v8a --enable-gl4es --enable-gles3compat
./waf build
# the game libraries build from game/ with the same waf flow (--android)
```

CI (`.github/workflows/build-apk.yml`) builds the APK + all game libraries on every push
and publishes them on the Releases page. If you don't want to set up the NDK locally,
push a branch and let CI build it, then sideload the artifact.

## Repository map

| Path | What lives there |
|---|---|
| `engine/` | Vendored Xash3D-FWGS fork. Our patches touch Android lifecycle, filesystem, log paths, the GL refs. Upstream commits are pinned in `engine/UPSTREAM.md`. |
| `game/` | Vendored Diffusion game code. The interesting part is `game/client/render/` — the custom renderer (r_main, r_shader, r_studio, r_surf, r_grass, r_postprocess...). |
| `game/client/render/glsl/` | The shader set; `shaderlist.txt` enumerates the uber-shader permutations. |
| `docs/PORTING_NOTES.md` | Numbered findings from every porting round — read this before changing anything in the render path. |
| `logs/dev-log.md` | Version-by-version history with root-cause writeups. |
| `tools/` | Offline validators (`validate_es300.py`, `audit_es_surface.py`, `audit_v053.py`) and the DXT decoder test. |

## Ground rules

1. **Read `docs/PORTING_NOTES.md` first.** Most "obvious" fixes in the render path
   have already been tried and documented (the readback cliffs, the shader compile
   economics, the DDS format matrix).
2. **Offline validation before device time.** `tools/validate_es300.py` compiles every
   shader permutation through glslang — a change to `glsl/` that breaks validation
   never needs a device round to find.
3. **Diagnostics travel with fixes.** The perf heartbeat, the startup banner
   (`half-D vN: ref=... renderer=...`) and the watchdog exist because several
   releases were debugged blind. If you touch the render path, keep them working.
4. **Device logs are the source of truth for performance claims.** The launcher
   archives engine.log + logcat per session (Settings → Session logs → Share).
5. Don't commit game assets — the licensing position is in `docs/PERMISSIONS.md`
   and `README.md`.

## Where help is wanted right now

- **The red/magenta grate lighting** in the chapter-select garage (normal-map decode
  vs. dynamic-light interaction; instrumentation is already in the renderer).
- **Scene-bound fps in heavy areas** — internal render resolution is still 1549x680
  at scale 1.0; the batching in the custom renderer is per-surface.
- **The native GLES3 ref (`gles3compat`)** works for textures now (CPU DXT decode in
  `engine/engine/common/imagelib/img_dds.c`) but the custom renderer still renders
  wrong on it — it's opt-in via `-ref gles3compat`.
- Missing-content cleanup: two vehicle models and ~40 base Half-Life sounds that some
  installs don't have (see the v0.8.9 notes).

## Commit style

Short imperative subject, body explains *why* (the diagnosis, not just the diff).
Look at `git log` — the history doubles as the porting documentation.
