# Research Log

## 2026-11-20 — Phase 0 research session

**Goal:** validate feasibility of porting Diffusion to Android as a standalone APK; choose the architecture; identify risks.

**Findings (details in RESEARCH.md):**
1. Diffusion is a Xash3D-FWGS total conversion (8–12 h SP + deathmatch MP), v1.21, ~2.8 GB, free on itch.io. No Android port exists. No OSS license, but dev permission obtained verbally.
2. Xash3D-FWGS has an official Android port (Gradle project, Waf build, touch controls, Mobility API). No engine porting needed — packaging + tuning.
3. Target device: Helio G100, Mali-G57 MC2 (Valhall), aarch64 Android 14. GLES 3.0 path is safe; panvk exists for Android Mali (panvk-kbase-android, arch v6–v14) but experimental.
4. WinlatorMaliPlus reviewed for component ideas: keep only BCn→ASTC transcode, panvk packaging, FSR-style upscaler concepts. Its Wine/Box64/Proton layer is irrelevant to a native FWGS port.
5. FSR1 (EASU+RCAS) implementable as GLSL ES 3.0 fragment passes — no Vulkan dependency.

**Decisions:**
- D1: Native fork of xash3d-fwgs; no emulation layer.
- D2: GLES 3.0 `ref_gl` is the default renderer; panvk is a flagged experiment.
- D3: Content ships as OBB / on-demand download, not repo commits (licensing).
- D4: Custom touch layout purpose-built for Diffusion's interaction scheme (stock layout inadequate).
- D5: FSR1 post-process pass in-engine with 50/65/75/100% scale presets.

**Open questions:**
- Q1: Which exact xash3d-fwgs commit does Diffusion v1.21 target? (Check Aynekko/Diffusion repo build scripts.)
- Q2: Does Diffusion ship BCn textures? (Scan WADs in Phase 2.)
- Q3: Written permission scope from Aynekko (asset redistribution in an APK).
- Q4: Does the Hot 50 4G SKU in hand have 4 or 8 GB RAM?

**Next actions:** Phase 1 — fork engine, set up NDK/Gradle build, baseline stock HL on device.
