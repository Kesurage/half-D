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
## 8. Renderer research — what Diffusion's renderer actually needs (v0.1.1 device test round 2)

v0.1.1 booted the game code (server + client DLLs load, all engine extensions
active) and then failed at `Host_ErrorInit: This game requires the OpenGL
renderer. GLES and OpenGL Core contexts are not supported.` Findings, all
source-verified:

1. **Architecture**: the renderer is compiled INTO the client DLL
   (`client/render/r_opengl.cpp`, Uncle Mike's Xash custom renderer lineage,
   GPL-3.0). It does not draw through the engine's renderer module; it only
   borrows the context the engine's ref module created and then runs its own
   GL 2.0-class pipeline. It rejects anything but `CONTEXT_TYPE_GL`
   (desktop compatibility profile; even GL Core is refused).
2. **Hard requirements** (renderer disables itself without them): GL 1.1+2.0
   function sets, draw_range_elements, ARB_multitexture, ARB_vertex_buffer_object,
   ARB_vertex_array_object, ARB_shader_objects, ARB_shading_language_100,
   ARB_vertex_shader, ARB_fragment_shader, ≥32 bones of vertex-uniform skinning.
   **Optional** (features degrade with warnings): EXT_texture_array (landscapes),
   occlusion queries, EXT_gpu_shader4 (omni shadows), depth_texture/shadow
   (dynamic shadows), FBO, program_binary.
3. **Shaders**: all 74 files in `glsl/` are GLSL 1.10 (no `#version` at all) —
   trivially convertible to GLSL ES in Phase R1 (no 120/130-only features to
   untangle first).
4. **Renderer selection**: engine tries `-ref <name>` cmdline, then `r_refdll`
   cvar (saved in `video.cfg` in the base dir), then autodetect (gles1 first on
   Android, which is why v0.1.1 used it).
5. **Route A — gl4es (v0.1.2, immediate)**: `ref_gl4es` is the engine's `ref/gl`
   built against gl4es, so `GL_InitExtensionsBigGL` reports CONTEXT_TYPE_GL —
   exactly what the game demands. Vendored gl4es implements VAOs (advertises
   GL_ARB_vertex_array_object, `src/gl/buffers.c`), VBOs, GLSL 110/120 →
   GLES translation. Our APK already ships `libref_gl4es.so`. Risks: translation
   overhead on a Mali-G57, possible gaps in FBO-heavy paths. Default since
   v0.1.2 via launcher argv `-ref gl4es`.
6. **Route B — native GLES3 renderer (Phase R1, the real fix)**: port
   `client/render` to GLES 3.0 + GLSL ES 300. The 110-only shader set makes
   this mostly mechanical (baked matrices → uniforms, fixed-function bits
   already gone). This is the performance/quality path; scoped in the roadmap.
7. **Ecosystem check (2026-11-21)**: Aynekko's GitHub (8 repos) has no Android
   work — `Diffusion-executable` is only the branded Windows launcher stub, no
   engine patches. PrimeXT (same SDK lineage, same client-side-renderer +
   PhysX profile) lists an Android port as an unfinished goal with no GLES
   strategy published. Nothing to reuse; half-D's Route B remains the
   differentiator.
8. **Fonts** (same device log): MainUI requests `gfx/fonts/FiraSans-Regular.ttf`
   (fallback `tahoma.ttf`, which we cannot redistribute — MS font). We bundle
   FiraSans-Regular.ttf (SIL OFL, `assets/fonts/`) into the libs zip at
   `diffusion/gfx/fonts/` since v0.1.2; tahoma stays a desktop-only fallback
   and the engine font fallback works regardless.
