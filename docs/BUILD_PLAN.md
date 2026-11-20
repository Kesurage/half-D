# Build Plan — half-D Android port

Goal: a **standalone APK** that installs and plays Diffusion on Android, tuned for the Infinix Hot 50 4G (Helio G100 / Mali-G57 MC2), with custom touch controls and FSR1 upscaling.

## Architecture decision

**Native port of Xash3D-FWGS (fork), not Winlator/emulation.** Diffusion already runs on FWGS; the engine has a first-class Android port. Wrapping the Windows build in Wine/Box64 (WinlatorMaliPlus route) would add translation overhead on a 2-core Mali GPU and break touch integration. WinlatorMaliPlus is used only as a reference for Mali-specific graphics techniques (BCn transcode, panvk packaging, Mali-tuned shaders).

## Phase 1 — Fork & baseline build

1. Fork `FWGS/xash3d-fwgs` into this repo as `engine/` (subtree or submodule; keep upstream history for easy rebases). Record the exact upstream commit Diffusion v1.21 targets (from Aynekko's Diffusion repo/configs).
2. Toolchain: Android NDK (LTS version used by upstream CI), JDK 17, Android SDK 34. Build native libs with Waf for `arm64-v8a` (primary) and `armeabi-v7a` (secondary):
   `./waf configure -T release --android=armeabi-v7a,aarch64` (follow `android/` Gradle wrapper; CI recipe from upstream `.github/workflows` is the reference).
3. Assemble APK via Gradle from `android/`, install on the Hot 50 4G, run stock Half-Life (user-owned `valve/` folder) to baseline: framerate, load times, audio, touch latency. Log results in `logs/device-tests/`.
4. Exit criteria: stock HL playable ≥ 60 fps at 100% internal resolution on A76 cores; logcat clean of engine crashes.

## Phase 2 — Diffusion integration & launcher

1. Copy the Diffusion game folder (`diffusion/`) into the APK assets or — better for 2.8 GB — as an **expansion file (OBB)** or on-demand download (Play Asset Delivery style, self-hosted fallback).
2. App package `com.halfd.diffusion`, launcher activity: single "Play" button plus settings (graphics presets, controls editor, FSR toggle). No "pick your game folder" UX — content path is fixed to the bundled Diffusion install.
3. Engine launch args: `-game diffusion` equivalent via Android `MainActivity` intent extras / config.
4. Verify: all 5 chapters load, save/load works, multiplayer menu reachable (networked play out of scope for v1 but must not crash).
5. Exit criteria: full campaign startable from the launcher with zero manual file placement.

## Phase 3 — Custom touch controls (see CONTROLS_PLAN.md)

Diffusion's non-standard interactions must be mapped to a custom touch layout, tuned per-scenario. This is our main original engineering work.

## Phase 4 — Graphics & performance

1. **Renderer**: `ref_gl` with GLES 3.0 context. Profile with `GPU inspector`/ Mali offline compiler for shader cost.
2. **FSR1**: offscreen scene FBO at selectable scale (50/65/75/100%), EASU+RCAS ported to GLSL ES 3.00 (AMD FSR 1.0, MIT). Add as a post-process pass in the renderer between world/HUD draw and swapchain blit. HUD option: draw HUD after upscale at native res (crisper text).
3. **BCn→ASTC transcode**: scan Diffusion WAD/textures for DXT formats; transcode at packaging time (tool in `tools/`), since Mali GLES has no native BCn sampling.
4. **Presets** (default "Hot 50 4G" preset):
   - Battery: 50% scale, no dynamic lights, 30 fps cap
   - Balanced (default): 65% scale + FSR, 60 fps cap
   - Quality: 100% scale, 120 fps cap (display permitting)
5. Optional experiment (behind a flag): **panvk** Vulkan driver via panvk-kbase-android for a Vulkan/compute renderer path. Not blocking release.

## Phase 5 — Android specifics & release

1. Android-specific fixes: scoped storage (SAF for any user content), immersive fullscreen + cutout handling, haptics via engine Mobility API, back-gesture handling, battery/thermal awareness (thermal throttle detection → auto-lower FSR scale), gyro aim option.
2. Packaging: signed release builds, GitHub Actions CI (NDK build + Gradle assemble), release APK + OBB per version.
3. Crash reporting: minimal, opt-in (ACRA or plain logcat uploader), logs to `logs/`.

## Repository layout (target, Phase 1+)

```
engine/        fork of xash3d-fwgs (upstream history preserved)
android-app/   launcher + settings + touch UI overlays
content-tooling/ OBB builder, BCn transcoder, touch-layout compiler
tools/         dev helper scripts
docs/ logs/    as today
```
