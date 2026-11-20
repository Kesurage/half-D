# Research Findings — Diffusion on Android via Xash3D-FWGS

Date: 2026-11-20. All findings verified against the linked sources at research time.

## 1. The game: Diffusion

- Developer: **Aynekko** (solo dev, mapper/musician). Itch.io page: https://aynekko.itch.io/diffusion — ModDB: https://www.moddb.com/mods/diffusion — GitHub org: https://github.com/Aynekko (Diffusion repository).
- Genre: first-person shooter, **total conversion** in its own universe, based on the **Half-Life SDK**, running on the **Xash3D-FWGS** engine (not vanilla GoldSrc).
- Content: extensive single-player story (**8–12 hours, 5 chapters**), plus a **multiplayer deathmatch** mode. You play James Smith, a SWAT operative stranded near an abandoned factory in Utah.
- Current version at research time: **v1.21** (Jul 2026), ~**2.8 GB** download.
- Desktop requirements: OpenGL 3.0 GPU, 1 GB VRAM/RAM, 6 GB disk, Win7+/Linux. Runs standalone via included `.bat`/`.sh` or as a Steam Half-Life mod (`hl.exe -game Diffusion`).
- **Controls: no official controller/touch support.** The game uses a **custom interaction scheme** (contextual actions, custom HUD/weapon logic per the community discussion); community gamepad configs exist but were hand-made. This is the single biggest porting task — see `CONTROLS_PLAN.md`.
- Licensing: **no open-source license stated**; game is free, donations accepted. **Dev permission to fork/port obtained verbally — must be captured in writing** (`PERMISSIONS.md`).
- Android: **not mentioned anywhere** — no Android port exists. half-D would be the first.

**Implication:** Diffusion is already a Xash3D-FWGS game. This is the best possible case: we do not need to port game code to a new engine, we need to package and tune the existing FWGS Android port around Diffusion's content and control scheme.

## 2. The engine: Xash3D-FWGS

- Repository: https://github.com/FWGS/xash3d-fwgs. Actively maintained (new releases add features like a `_wheel` touch control and an updated Android adaptive icon; releases page: https://github.com/FWGS/xash3d-fwgs/releases).
- **Official Android port exists**: APK from Play Store ("Xash3D FWGS") or continuous builds; the repo contains an `android/` directory usable from **Android Studio or Gradle** directly.
- Build system: **Waf** (Python-based) for the native engine; the Android app wraps the built native libs via Gradle. Supported ABIs include ARM (armv7/aarch64) for Android.
- Mobile features already in the engine: **touch controls** (virtual buttons, edit mode), gamepad input, **Mobility API** (vibration, better mobile game integration), multiple input methods.
- Renderers: `ref_gl` (OpenGL/GLES) is the stable path; engine also supports software-ish/lighter paths and newer renderer work. GLES 3.0 covers Diffusion's OpenGL 3.0 requirement on paper, but shader/entity extension limits of Diffusion must be tested (it uses extended FWGS features).
- Users regularly run Half-Life and GoldSrc mods on Android 13/14 devices with the current engine (community reports, e.g. XDA/Reddit threads and the FWGS wiki).

**Implication:** Phase 1 is low-risk: build the stock engine APK ourselves and run stock Half-Life on the Hot 50 4G to baseline performance before touching Diffusion.

## 3. Target device: Infinix Hot 50 4G / Helio G100

- SoC: **MediaTek Helio G100** (6 nm): 2× Cortex-A76 @ 2.2 GHz + 6× Cortex-A55 @ 2.0 GHz (https://www.mediatek.com/products/smartphones/mediatek-helio-g100, https://nanoreview.net/en/soc/mediatek-helio-g100).
- GPU: **ARM Mali-G57 MC2** (Mali-G57-class, Valhall architecture; two cores). (MediaTek product page confirms "Arm Mali-G57".)
- RAM: 4–8 GB depending on SKU; display typically HD+/120 Hz on this model line.
- aarch64 Android (14). GoldSrc-class workloads are CPU-light; A76 cores are plenty. The GPU is the constraint for eye-candy (FSR1 helps by rendering below native and upscaling).

## 4. Graphics stack: GLES first, panvk as an experiment

- The vendor driver on Android provides GLES 3.x; the engine's `ref_gl` renderer will use it directly. **This is the default and safe path.**
- **panvk** is Mesa's open-source Vulkan driver for Mali (Bifrost and later; Mali-G57 is Valhall, within its supported range). Status: in-tree in Mesa, conformant for newer Mali generations and under active development (Mesa docs: https://docs.mesa3d.org/drivers/panfrost.html; the new "Kraid" compiler became default for Valhall v9+ in Mesa 27.0).
- On Android specifically, the vendor kernel driver is `mali_kbase`; **https://github.com/zenithblue-oss/panvk-kbase-android** ships standalone panvk builds for Android Mali GPUs (Bifrost, Valhall, 5th Gen — arch v6 to v14) talking directly to `mali_kbase`. This is the component WinlatorMaliPlus uses for its experimental panvk option.
- **WinlatorMaliPlus** (https://github.com/adesyaefull/WinlatorMaliPlus) demonstrates the Android Mali toolbox we can borrow ideas from: panvk as an *optional, experimental* driver delivered in Adrenotools-style ZIP packages, BC1–BC7 → ASTC/ETC2 texture transcoding (Mali lacks native BCn!), Mali-tuned compute shaders, FSR/SGSR upscalers, and sysfs GPU telemetry.
- **Important caveat for us:** Winlator's whole Wine/Box64/Proton stack is irrelevant — Diffusion runs natively on FWGS, no Windows emulation needed. Only the Mali-specific graphics techniques are relevant.
- **BCn caveat:** if Diffusion ships BC1/BC7 (DXT) textures, they will not sample natively on Mali GLES; plan a transcode step to ASTC/ETC2 at packaging time (Phase 4) exactly like WinlatorMaliPlus does.
- Vulkan usage would only matter if we adopt a Vulkan-based renderer or FSR via compute; for FSR1 a **GLES fragment-shader pass** is sufficient and avoids the experimental-driver risk entirely.

## 5. FSR1

- FSR 1.x is AMD's spatial upscaler: EASU (edge-adaptive upsample) + RCAS (sharpen) — two fragmentshader passes, no motion vectors, no temporal history. Ideal for a GoldSrc-class game on a 2-core Mali GPU.
- Implementation plan: render the 3D scene to an offscreen FBO at e.g. 50–75% resolution, run EASU+RCAS (ported to GLSL ES 3.0 from AMD's FSR 1.0 sample, MIT-licensed), blit to screen. UI/HUD drawn at native or included in the pass — see `BUILD_PLAN.md` §FSR.

## 6. Risks (top 5)

1. **Diffusion's engine-version coupling** — it relies on bleeding-edge FWGS features; we must fork the exact engine version the mod targets and track upstream.
2. **Custom controls translation** — biggest design task; no one has done it for this game.
3. **Mali-G57 MC2 fill-rate** — mitigated by FSR1 + sensible defaults (see `DEVICE_NOTES.md`).
4. **Content licensing** — 2.8 GB of assets; redistribution permission scope must be written down before any release.
5. **panvk immaturity** — keep it strictly optional/experimental; GLES path must ship first.

## Sources

- https://aynekko.itch.io/diffusion
- https://www.moddb.com/mods/diffusion
- https://github.com/Aynekko
- https://github.com/FWGS/xash3d-fwgs (+ /releases)
- https://www.mediatek.com/products/smartphones/mediatek-helio-g100
- https://nanoreview.net/en/soc/mediatek-helio-g100
- https://docs.mesa3d.org/drivers/panfrost.html
- https://github.com/zenithblue-oss/panvk-kbase-android
- https://github.com/adesyaefull/WinlatorMaliPlus
