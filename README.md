# half-D

**Diffusion (by Aynekko) — native Android port. A fork of [Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) delivering a standalone, playable APK with a built-in launcher, custom touch controls, FSR1 upscaling and Mali/panvk-friendly graphics settings.**

**Current state (v0.8.x):** playable. The full custom renderer runs through gl4es with the mobile preset (grass/mirrors/MSAA off, 60 fps cap), and a persistent shader-binary cache plus `.tried` markers keep launch times down. Known remaining work: scene-bound 13-17 fps in the heaviest areas (see the v0.8.11 release notes), the red/magenta grate lighting bug, and missing content files in some installs. History and diagnosis for every build are in `logs/dev-log.md` and the release notes.

CI builds the Diffusion Android libraries (client/server/PhysX/MainUI, arm64-v8a)
on every push; grab them from [Releases](https://github.com/Kesurage/half-D/releases).
Note: the itch.io/ModDB download ships PC binaries only (Windows x86/x64,
Linux amd64) — a phone is ARM, so all game code is recompiled from the source
vendored here (see `docs/WHAT_TO_DOWNLOAD.md` § "amd64 vs arm64").

- Target device: **Infinix Hot 50 4G** (MediaTek Helio G100, ARM Mali-G57 MC2, Android 14)
- Source game: **Diffusion** — a total-conversion Half-Life mod running on Xash3D-FWGS (https://aynekko.itch.io/diffusion, https://www.moddb.com/mods/diffusion)
- Engine base: Xash3D-FWGS (already has an official Android port, Gradle project, touch controls, Mobility API)

## Why a fork?

Diffusion is a *total conversion*: it pushes Xash3D-FWGS beyond stock GoldSrc limits and ships **custom, non-standard controls** (contextual interactions, custom HUD/weapons). The stock FWGS APK + dropped-in mod folder gives a mediocre experience on Android: default touch layout, no per-mod tuning, no upscaling, no performance presets for a Mali-G57 class GPU. half-D aims to be a **single standalone APK** ("launch and play"), not "install engine + copy files".

## Repository layout

```
engine/        Vendored Xash3D-FWGS fork (the engine + Android launcher project)
game/          Vendored Diffusion game code (client renderer, server, PhysX)
mainui/        Vendored MainUI (menu library)
docs/          Research findings, roadmap, build plan, controls plan, device notes
tools/         Build/test helpers (GLSL validators, DXT decoder test, adb capture)
tools/releases Release scripts, one per shipped version
scripts/       Repo maintenance and asset generation scripts
logs/          Development log, research log, device test results
deliverables/  Generated reports (PDF)
```

Contributions welcome — see `CONTRIBUTING.md` for the build setup and where to start.

## Roadmap (summary)

| Phase | Goal |
|-------|------|
| 0 | Research & planning (this repo state) — **done** |
| 1 | Fork xash3d-fwgs, build stock Android APK from source, verify stock HL runs on the Hot 50 4G |
| 2 | Integrate Diffusion content (licensed by permission of Aynekko), launcher UI, game detection |
| 3 | Custom touch-control scheme adapted to Diffusion's non-standard interactions |
| 4 | Graphics & performance: GLES3 renderer tuning, FSR1 post-process pass, Mali-friendly settings |
| 5 | Android-specific fixes, packaging (expansion APK/OBB), signed release builds + CI |

See `docs/ROADMAP.md` and `docs/BUILD_PLAN.md` for details.

## Licensing / permission

- Diffusion by Aynekko: **no open-source license attached**; the developer has **given explicit permission** to fork and port for this project (record in `docs/PERMISSIONS.md` before any content is committed). The game itself is free on itch.io.
- Xash3D-FWGS engine: check `LICENSE` in the upstream repository before forking; keep upstream license and attribution intact.
- half-D project code (our own tools/patches): MIT.

Nothing of Diffusion's assets will be committed to this repo without written permission on file.
