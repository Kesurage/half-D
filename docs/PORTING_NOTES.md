# Porting Notes — what "Diffusion needs different components" actually means

The community was right. Diffusion is not a normal GoldSrc/Xash mod; it brings its
own graphics stack. This note records what we found in the source (all verified in
the vendored tree) and the plan for each piece on Android.

## 1. Custom client-side renderer (the big one)

`client/render/` (~30 files: r_main, r_surf, r_studio, r_grass, r_shadows,
r_cubemaps, r_postprocess, r_light, r_particle…) is Uncle Mike's Xash3D custom
renderer (GPL-3.0). The client DLL drives raw OpenGL itself on top of the engine's
context — it does not use the engine's built-in renderer for world drawing.

On desktop it requests **desktop GLSL 1.20**: the shaders use built-ins that do
not exist on OpenGL ES:

| Desktop GLSL used in glsl/*.glsl | GLES 3.00 replacement |
|---|---|
| `gl_ModelViewProjectionMatrix`, `gl_ModelViewMatrix` | pass as uniforms (the C++ code already computes these matrices) |
| `gl_Vertex`, `gl_Normal`, `gl_MultiTexCoord0`, `gl_Color` | generic vertex attributes (`in` + `glVertexAttribPointer`) |
| `varying` / `attribute` | `out` / `in` |
| `texture2D`, `texture1D` | `texture()` |
| `gl_FragData[n]`, fixed blend tricks | single color out + explicit blend states |

**Plan (Phase R1):** mechanical GLSL 1.20 → GLSL ES 3.00 conversion of the ~50
files in `game/glsl/` (bmodel, grass, shadows, SSAO, HBAO, bloom, god rays,
dronescreen, glitch, FSR passes), plus a small shader-preprocessor in
`r_shader.cpp` that injects a compatibility header (defines `texture2D`→`texture`,
attribute layout, uniform matrices) so we keep one shader source. Precedent for
this exact work: GLES ports of Uncle Mike's renderer exist in the Xash community.

## 2. FSR1 is already in the mod

`glsl/fsr_easu_fp.glsl` + `glsl/fsr_rcas_fp.glsl` — Diffusion ships AMD FSR 1.0
itself (EASU + RCAS) inside its postprocess chain. Our original "implement FSR1"
goal is now "make the mod's own FSR work on GLES", which rides the same Phase R1
shader conversion. Nice: one pipeline gives us SSAO/bloom/FSR on Android for free
once the renderer runs.

## 3. NVIDIA PhysX 4.1 (server side)

`server/physics/physx/` implements ragdoll/debris physics; `server/wscript`
compiles PhysX **from source** using `scripts/physx_manifest.json` (a
platform-agnostic source list: `sources_unix` covers non-Windows). For Android we
need `sources_unix` to work under the NDK's clang; watch for:
- `PX_ANDROID` / platform defines in `manifest['defines']` (verify vs
  physx_manifest.json keys),
- SSE-only code paths (PhysX has NEON paths; manifest may need an
  `sources_android` variant),
- exceptions/RTTI flags (already `-fexceptions` in wscript).

If aarch64 compiles clean → full physics. If not → Phase R2 fallback: stub the
PhysX interface with simple kinematic physics (the impl is behind
`server/physics/physx/physx_impl.*`, one seam).

## 4. MainUI (custom menu) — already Android-aware

`Diffusion-MainUI` ships an `Android.mk` (the dev anticipated Android) and
defaults to stb_truetype, so no freetype needed. Builds as a waf subproject at
`game/3rd-party/mainui_cpp` (CI copies our vendored copy into place).

## 5. Engine backends on Android

Our engine pin builds `ref_gles1` (nanogl) and `ref_gl4es` (gl4es) on Android.
The client's custom renderer speaks desktop-GL-ish calls through the same context;
gl4es is the safety net (it translates desktop GL 2.x → GLES 2.0). Test matrix:
1. Custom renderer + GLSL ES conversion on native GLES 3.0 context (target),
2. Custom renderer unmodified through gl4es (fast fallback if conversion lags),
3. Engine's own renderer with the mod content (last-resort fallback; visual
   features degraded).

## 6. What this changes in the roadmap

- Phase 2/3 unchanged (launcher, content packaging).
- Phase 3 "controls" unchanged (Diffusion's custom HUD/inputs are in client
  source we vendored; `client/input_xash3d.cpp` already has Xash-specific input
  glue — good sign for touch integration).
- Phase 4 becomes: R1 shader conversion → FSR/SSAO on GLES; BCn transcode still
  applies only if textures ship DXT (check WADs when content is downloaded).
- New Phase R2: PhysX aarch64 build verification (started in CI now).

## 7. Android library loading — ground truth from the v0.1.0 device test

v0.1.0 failed on device with `dlopen failed: library "libclient_android_arm64.so"
not found`. Source-verified cause and fix (engine `engine/platform/android/lib_android.c`,
`engine/common/lib_common.c`, `3rdparty/library_suffix`):

1. **Naming**: `COM_GenerateCommonLibraryName` on Android produces
   `lib<name>_android_arm64.so` (mandatory `lib` prefix). The engine requests
   `bin/libserver_android_arm64.so`, `bin/libclient_android_arm64.so` (paths
   from gameinfo `dll_path`/`gamedll_linux`), and for the menu first
   `bin/libmenu_android_arm64.so` then bare `libmenu.so`. Our v0.1.0 zip shipped
   `client_android_arm64.so` etc. — no `lib` prefix, so every lookup missed.
2. **Load chain** (`ANDROID_LoadLibrary`, in order): `XASH3D_GAMELIBDIR/<name>`
   env → VFS (game folder on /storage, full-path dlopen) → **bare-name dlopen =
   the APK's own `lib/<abi>/`**. The last step is the reliable one on modern
   Android (scoped storage makes /storage dlopen device-dependent; the APK
   nativeLibraryDir always works, and a VFS failure falls through to it).
3. **Fix (v0.1.1)**: CI bakes the three game libs into the APK
   (`jniLibs/arm64-v8a/`: `libserver_android_arm64.so`,
   `libclient_android_arm64.so`, `libmenu.so` — the bare fallback name for the
   menu), and the libs zip now carries correctly-named files for the
   game-folder route. Both routes fail soft: whichever is loadable wins.
4. **Not a bug**: package `su.xash.engine.test` — the `continuous` buildType is
   `initWith(release)` + committed debug keystore + `.test` suffix by design
   (upstream FWGS CI convention; `assembleRelease` is unsigned → uninstallable).
   The `bfd42b7` in the log's version string is the throwaway hash of CI's
   in-workspace `git init` of the vendored engine (needed because gradle reads
   `git rev-parse HEAD`), not our upstream pin (11ed246c — see engine/UPSTREAM.md).
5. **Related mechanism, unused by us**: `rodir` = `<app files>/gamelibs` is
   populated by the launcher's `GameLibDownloader` (FWGS serves per-game
   Android libs for known games; nothing for Diffusion). `armeabi-v7a` game
   libs are not built — v7a phones get the engine but no game code (device is
   arm64; documented, not a goal for M1).
