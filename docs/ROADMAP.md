# Roadmap

| Phase | Name | Deliverable | Depends on |
|---|---|---|---|
| 0 | Research & planning | This repo: docs, tools, logs, PDF report | — |
| 1 | Fork & baseline | `engine/` fork of xash3d-fwgs; CI builds stock APK; stock HL validated on Hot 50 4G; benchmark log | Phase 0 |
| 2 | Diffusion integration | Launcher APK (`com.halfd.diffusion`), content via OBB/download, full campaign loadable | Phase 1 |
| 3 | Custom touch controls | `diffusion.layout` + adaptive widgets, bindings table, per-chapter test checklist green | Phase 2 |
| 4 | Graphics & performance | FSR1 GLES pass, BCn→ASTC transcoder, presets (Battery/Balanced/Quality), benchmark deltas | Phase 2 |
| 5 | Release | Android-specific fixes (storage, cutout, haptics, thermal), signed releases, CI, crash logs | Phases 3+4 |
| 6 (stretch) | Experiments | panvk-based Vulkan renderer path; multiplayer networking QA | Phase 4 |

## Milestone acceptance

- **M1 "It boots"** — stock engine APK built from our fork runs HL on the target phone.
- **M2 "It's Diffusion"** — campaign chapter 1 completable start-to-finish from the launcher, no PC needed.
- **M3 "It feels native"** — controls checklist (CONTROLS_PLAN) green; a new player can play without instructions.
- **M4 "It performs"** — Balanced preset holds 60 fps p95 in combat on the Hot 50 4G.
- **M5 "v1.0 release"** — signed APK + OBB on GitHub Releases, install-and-play.
