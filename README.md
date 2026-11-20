# half-D

**Diffusion (by Aynekko) — native Android port. A fork of [Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) delivering a standalone, playable APK with a built-in launcher, custom touch controls, FSR1 upscaling and Mali/panvk-friendly graphics settings.**

> Status: **Phase R — port engineering.** Engine fork, Diffusion source, MainUI
> and build tooling are vendored; CI builds the engine APK and Android game
> libraries on every push (see Actions). Phase 0 research lives in `docs/`.

- Target device: **Infinix Hot 50 4G** (MediaTek Helio G100, ARM Mali-G57 MC2, Android 14)
- Source game: **Diffusion** — a total-conversion Half-Life mod running on Xash3D-FWGS (https://aynekko.itch.io/diffusion, https://www.moddb.com/mods/diffusion)
- Engine base: Xash3D-FWGS (already has an official Android port, Gradle project, touch controls, Mobility API)

## Why a fork?

Diffusion is a *total conversion*: it pushes Xash3D-FWGS beyond stock GoldSrc limits and ships **custom, non-standard controls** (contextual interactions, custom HUD/weapons). The stock FWGS APK + dropped-in mod folder gives a mediocre experience on Android: default touch layout, no per-mod tuning, no upscaling, no performance presets for a Mali-G57 class GPU. half-D aims to be a **single standalone APK** ("launch and play"), not "install engine + copy files".

## Repository layout

```
docs/          Research findings, roadmap, build plan, controls plan, device notes
tools/         Helper scripts (build env check, adb log capture, touch-layout helper)
logs/          Development log, research log, device test results
deliverables/  Generated reports (PDF) and release notes
```

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
