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

## 9. v0.1.2 device log post-mortem: why -ref gl4es never reached the engine

The v0.1.2 device log still showed `Loading renderer: gles1 -> libref_gles1.so`
with no gl4es attempt at all. Root cause chain (all source-verified):

1. The APK was fine: `classes.dex` in the released v0.1.2 APK contains the
   patched default string (verified via strings on the artifact). The client
   build date in the log matches the v0.1.2 CI run.
2. But the launcher never used it: `Game.kt` builds argv as
   `"-game diffusion" + pref.getString("arguments", "-console -log")` and
   ALWAYS puts it as the `argv` intent extra — so `XashActivity`'s
   `argv == null` fallback (where `-ref gl4es` lived) is dead code for every
   normal launch. The engine command line therefore had no `-ref` at all.
3. With no `-ref` and (on a fresh install) no `r_refdll`, the engine falls to
   autodetect, whose hardcoded order starts with gles1 on Android
   (R_CollectRendererNames in ref_common.c).
4. Worse, after a successful gles1 load the engine WRITES `r_refdll gles1`
   into video.cfg — so even passing `-ref` once wouldn't stick for future
   launches; the saved cvar would keep winning over any launcher default.

Fix (v0.1.3), layered so every launch path is covered:
- engine C: on XASH_ANDROID try gl4es right after an explicit `-ref` argument
  and BEFORE the saved r_refdll cvar (stale video.cfg can no longer win);
  explicit `-ref` on the command line still overrides everything; failure
  falls through to the normal cvar/autodetect chain.
- launcher: Game.kt default arguments `-console -log -ref gl4es`.
- XashActivity: null-argv default also `-console -log -ref gl4es`.
- font: FiraSans-Regular.ttf now also ships as an APK asset and is extracted
  to `<rodir>/<gamedir>/gfx/fonts/` at launch (v0.1.2 only put it in the
  optional libs zip, which wasn't installed on the device).

## 10. v0.1.3 device log post-mortem: shaders are GLSL 1.30-only by header, context is GL 2.1

Huge progress in the v0.1.3 log: gl4es loaded (`GL4ES using Mali-G57 MC2`,
GL_VERSION 2.1, GL_MAX_* limits all present), the client renderer's
context-type check passed, and the crash moved into the game's own shader
system: `Host_ErrorInit: assert failed at ../client/render/r_shader.cpp:1546`
— the GenSunShafts uniform init got a NULL shader.

Why NULL: GL_ProcessShader prepends `#version 130` to every shader it
compiles. gl4es is a GL 2.1 context (GLSL 1.20 max) — a #version 130 shader
cannot compile there, so every shader fails and the first init assert-fatal'd.

Why the log showed no compile errors: the client's ALERT() returns early
when developer_level == 0 — and our default argv had no -dev. Every
"Couldn't compile" / info-log message was generated but silently dropped.
(The glsl files were NOT missing: a missing file would also have logged, at
the same silenced level — but the fix below logs that unconditionally now.)

Fix (v0.1.4, game fork):
- version chosen from glConfig.version: 130 only on GL 3.0+, else 120/110;
  the shader sources themselves are classic GLSL 1.10 style (varying,
  gl_FragColor, texture2D) so 120 works for the core world shaders;
- `#define texture texture2D` injected below GL 3.0 (dronescreen and
  waterdrops use the 130-era generic texture() call; waterdrops' bias
  argument is legal for texture2D in fragment shaders);
- shader failure/missing-file messages now print unconditionally via
  Con_Printf, and the launcher passes -dev 2, so the next device log will
  name any shader that still fails and why;
- the 34 init-time uniform inits are guarded (if shader) — a failing
  optional effect shader (SMAA, FSR, glitch, emboss, waterdrops...) is
  skipped instead of crashing the game; the runtime compile loop and the
  postprocess CheckShader guards already tolerate failed shaders;
- known residual: smaa.h / emboss.h / glitch_fp.glsl / alpha2coverage.h use
  textureLod/textureSize (GLSL 1.30+ only) — those effects will stay off on
  GL 2.x until a follow-up compat pass; not needed for core rendering.

## 11. v0.1.4 device crash post-mortem: map-load OOB + the real shader errors

v0.1.4 reached the menu (FiraSans rendered, config, achievements, sprites)
and started loading the first map — then SIGSEGV in
`Mod_LinkLeafLights (r_world.cpp:760)`:

- Crash: after the last leaflight is consumed the loop keeps dereferencing
  `lp` one past the array end on every remaining leaf (`j == numleaflights`
  is not checked in the outer condition). On desktop it reads heap garbage;
  on Android's hardened allocator the read hits a guard page (SEGV_ACCERR).
  Fixed with `j < world->numleaflights &&` — semantics otherwise identical.
- Shaders: the log listed 27 failed shaders but NO driver error text (the
  at_error ALERTs and the info-log Msg are gated by developer level; the
  -dev 2 we pass had not taken effect at client-init time). Correlation
  found by diffing the init list: 100% of the failed shaders #include
  mathlib.h, 100% of the passed shaders don't. mathlib.h (and matrix.h,
  shadow_omni.h, studiosolid_vp.glsl, terrain.h, tnbasis.h) use the
  desktop-GLSL `const in` parameter qualifier — GLSL ES 1.00 (gl4es target)
  allows only one qualifier per parameter. mathlib.h also relies on implicit
  int->float conversions, which ES 1.00 forbids. All swept/fixed; and shader
  compile/link errors now print the driver info log unconditionally, so the
  next log names any remaining offender with the compiler's own message.

## 12. v0.1.5 device crash post-mortem: desktop-generated studio caches

v0.1.5 got past the map-load OOB (the Mod_LinkLeafLights guard held) and
deeper into map load — then `Host_ErrorInit: assert failed at
../client/render/r_studio.cpp:1733`, the TBN sanity check in
StudioCreateMeshCache.

What that code does: for bump-mapped studio models the game stores
tangent/binormal/normal vectors in `cache/<model>.tbn` files. The itch
package ships such caches, generated by the author's Windows build. They
pass the ident/version/modelCRC validation, but the assert compares each
submodel's cached `vertex_offset` against the running vertex count this
build computes — mismatch → assert → dead game. A `.tbn` cache is simply
not portable across builds.

Same landmine next door: the vertex-light lump (dml, embedded in the BSP)
goes through the same pattern at r_studio.cpp:1711-1712, and its
`ASSERT(j != MAXSTUDIOMODELS)` was followed by an unconditional
`dml->submodels[j]` index — an out-of-bounds read waiting to happen.

Fix (v0.1.6, game fork):
- StudioLoadTBN returns false unconditionally: TBN is always regenerated
  in-process (deterministic; a fraction of a second per model on the
  device). The .tbn files shipped with the game are ignored.
- both sanity checks are non-fatal warnings now (visible in engine.log),
  and the dml block got the missing bounds guard.

Note: this round we only got the screenshot (the assert dialog) — the
unconditional shader diagnostics from v0.1.5 are in the device's
engine.log but weren't sent; worth requesting next time.

## 13. v0.1.6 device post-mortem: the game renders; shaders never updated on device

Huge milestone: v0.1.6 got through map load, studio models and started
drawing the world — crash moved to R_DrawGrass (r_grass.cpp:1704), SIGSEGV
at 0x524. And this time the engine.log (76 KB) carried the driver's own
error text thanks to the unconditional diagnostics:

- `S0001: Type mismatch in arithmetic operation between 'int' and 'float'`
  — but the repo's mathlib.h has no such arithmetic left since v0.1.5.
- The 27 failing shaders were IDENTICAL to v0.1.4. Same sources compiled.

Root cause: **the APK ships no glsl at all**; shaders load from the game
data folder on the device, which still contains the ORIGINAL itch glsl/.
The engine's filesystem order puts the game dir above APK assets
("unpacked files have the priority over packed files",
FS_AddGameDirectory) — so every glsl patch we ever made never reached the
device. The grass crash was collateral: glsl.genericFog is NULL after the
failed compile, GL_BindShader(NULL) leaves RI->currentshader NULL, and
the very next line reads u_FogParams at offset 0x524 of that NULL pointer
— matching the fault address exactly.

Fix (v0.1.7):
- engine: FS_Rescan mounts the engine APK assets a second time as the
  LAST search path — prepended, so it takes priority over the game
  folder. Patched glsl/ now ships in the APK assets and wins.
- android.c honors the mount path as the search filename (the package
  name made the second mount dedupe into a no-op).
- r_grass.cpp: both grass passes guard the currentshader deref.
- CI copies game/glsl into engine/android/app/src/main/assets/ (gradle
  already stores assets uncompressed via noCompress).

## 14. v0.1.7 device post-mortem: world renders; bloom trips the driver

Massive round: the world now renders end-to-end (map load, studio models,
brush list, grass, post effects start), and the APK-assets glsl override
from v0.1.7 WORKS - shader failures dropped 27 -> 10.

The crash moved to Bloom (r_postprocess.cpp:1065): glCopyTexSubImage2D on
a screen-sized NPOT texture that has a mip chain + mip-level FBO
attachments. Under GLES2 glGenerateMipmap cannot allocate mips for NPOT
textures, so levels 1+ are unallocated; gl4es implements the copy as
readback + glTexSubImage2D, and its software-mipmap chain (texture.c:1755)
then uploads into the missing levels - the Mali driver faults in memcpy
(SEGV_ACCERR). ToneMap (auto exposure, `gl_exposure` defaults 1) runs
right after Bloom with the same pattern and would crash next.

Of the 10 remaining shader failures, 8 were trivial ES 1.00 violations
now fixed (int literals in float math, round(), %, int overload arg).
Glitch (uint bitops) and SMAA (textureLodOffset) are GLSL 130-only and
stay off - both are cvar-gated (glitch is gameplay-triggered, gl_smaa
defaults 0).

v0.1.8:
- 8 shader files fixed for ES 1.00 (drawshafts, motionblur, heat,
  lensflare, waterdrops, generate_luminance, grassdepth_vp, dronescreen)
- Bloom + ToneMap gated on GL 3.0+ (NPOT mip chains are GL3-class)
- CheckShader guards on every post effect (a missing shader skips the
  effect instead of NULL-derefing; upstream only guarded RenderSunShafts)
- GrassDepth compiles again -> grass depth/shadow pass is back

## 15. v0.1.8 device post-mortem: it loads, it runs, at 0-1 fps

First fully playable load: the user got in-game (sounds, NPCs, achievements
all live). Three new findings:

1. **Mixed-case shader names missed the APK override.** AAssetManager_open
   is case-sensitive; GL_InitGPUShader builds filenames from program names,
   and the world/entity families use mixed case (GrassDepth, BmodelSolid,
   BmodelDlight, StudioSolid, StudioDlight). All of them fell through to
   the UNPATCHED year-old glsl/ in the game folder - 131 failed compiles
   over one session (textureSize / array constructors / '%' etc.), and
   every affected surface was skipped (invisible bmodels and studio
   models). Fix: case-insensitive retry in FS_FindFile_AndroidAssets.
2. **emboss.h / alpha2coverage.h are the reason those shaders need the
   override at all**: textureSize() and float[]() constructors are GLSL
   120/130-only. Once the override reaches them they still fail - so both
   headers now carry GL4ES-guarded no-op stubs (emboss detail filter,
   mip-based alpha rescale). Desktop GL is unchanged.
3. **0-1 fps**: shadow maps are already self-disabled (gl4es does not
   advertise GL_ARB_shadow), so the killer is per-frame full-screen
   readbacks. Sun shafts copy color AND depth back every frame: the color
   copy is a full-screen CPU readback through gl4es, the depth copy fails
   (GLES2 cannot read depth) and produces a garbage mask. Gated on
   GL 3.0+ like bloom. Also: 131 failed shader compiles per session are
   pure waste (each ~100-300 ms on Mali) and mostly disappear with fixes
   1+2. Second APK asset shipped: a generated touch_default button pack -
   the engine ships no touch button art, so every on-screen button was
   invisible (tappable but unseeable) - plus gfx/touch/stick_thumb and
   numbers.cfg weapon slots.

Next levers if fps is still bad after v0.1.9: grass density cvars, the
shader-compile storm at map load (GL_ARB_get_program_binary caching), and
a proper GLES2 sun-shafts implementation (half-res color copy).

## 16. v0.1.9 post-mortem: the case fix found it, then dropped it

The v0.1.9 log was perfect evidence: every Bmodel*/Studio* shader now
failed to LOAD ("couldn't load glsl/StudioSolid_vp.glsl" x169) instead of
compiling year-old sources. Root cause: my case-insensitive fallback
found the asset but still returned the original mixed-case name in
`fixedname` - the string the caller then opens the file through. The
search stopped at the APK ("found!") yet nothing could be read: worse
than v0.1.8's fall-through. Fix: report the case-corrected name.

The "3 fps increase" pointed at the real per-frame cost. With sun shafts
already gated, the remaining full-screen copies were still slow: screen
textures were created without alpha -> engine picked GL_RGB8/GL_RGB5 ->
gl4es only forwards glCopyTexSubImage2D to the driver for
RGBA/UNSIGNED_BYTE textures, so EVERY post-effect copy (motion blur,
screen water, monochrome, sharpen...) became a 4 MB CPU readback PER
FRAME PER EFFECT. TF_SCREEN now includes TF_HAS_ALPHA: RGBA textures,
native GPU copies, effects stay on. Depth copies remain impossible on
GLES2 (SSAO defaults off, sun shafts gated).

Also: the sandbox restored a stale Nov-20 clone again this round (HEAD
at v0.1.0-era, no v0.1.9 files) - recovered with fetch + reset to
origin/main before patching. Check `git log --oneline -1` first, every
session.

## 17. v0.2.0 post-mortem: the ground was an int literal

The v0.2.0 log narrowed the world to one error: every BmodelSolid/
BmodelDlight failure carried BMODEL_INTERIOR or BMODEL_WATER_REFRACTION,
and Mali's S0001 'cannot convert from int to float' pointed at the only
int-literal float consts in the whole GLSL tree: interior.h's
'const float staticSeed = 500;' and u_zFar = -4096 in both bmodel
shaders. Desktop GLSL 120 accepts those; strict GLSL ES 1.00 — which is
what the Mali GLES2 path enforces through gl4es — has no implicit
conversions at all. Three '.0' suffixes restore the terrain, interiors
and water.

The dips had a separate cause: upstream wipes a failed program without
registering it in the hash, so every runtime request for a broken
permutation re-ran a full driver compile (100-300 ms on Mali). v0.2.1
adds a negative cache (fail fast) plus the diagnostics layer the device
tests needed all along: per-program compile trace with options and
duration, a 5-second perf heartbeat (fps / avg / worst frame ms / scene
polys / negcache hits) and individual >150 ms spike lines — all gated by
gl_diag, default on, failures always printed.

Still open from this round: glitchy character animations (studio
sequence lerping — needs the heartbeat data), two missing car models
(bmec/bmec2 — not in the mod's shipped content), missing HL1 pain
sounds (mod references sounds it doesn't ship), and chapter-transition
save probes (save/ch1map1a.HL1 'couldn't open' — the game probes
optional saves, seems benign, watching it).

## 18. v0.2.1 post-mortem: the heartbeat pays off

First instrumented run. The negative cache works (0 suppressed retries —
each broken permutation compiles once per session), fps is now legible:
12-17 fps at 80-100k studio polys, 24-34 fps at 25-35k, 49 fps idle.
Studio geometry (epoly) is the dominant frame cost; the animated menu
background alone draws 280k studio polys. Two new facts:

1. The interior shaders progressed exactly one bug deeper:
   'No matching overload for randomFunction2' — 26 calls pass the int
   literal 1 for a float seed parameter. ES 1.00 overload resolution
   does no implicit conversions either. Fixed, plus two mix(0,...)
   sites found by the same sweep.

2. Chapter loads take 32-36 SECONDS (single-frame spikes). Added
   engine-side load-stage timing (world model, submodels, game-DLL
   activate, settle frames) — the next log will say which stage eats
   it. Suspects: thousands of small file opens through Android FUSE
   storage, or model/sound processing in the game DLL precache.

Still open: glitchy character animations (needs a look once the ground
renders — may be timing starvation at 12 fps), missing car models and
HL1 pain sounds (mod content, not the port), chapter-save probes.

## 19. v0.2.3: the validator round

No device log this time — we built one instead. `scripts/validate_es100.py`
replays the ref's exact preprocessing (prefix defines, include expansion,
permutation defines from r_shaderlist.h) for all 384 permutations and
compiles each with glslangValidator under GLSL ES 1.00 rules, emulating
gl4es translation (precision qualifiers in, f-suffixes out, legacy GL
matrices stubbed). Mali's strictness is fully reproducible offline.

Findings, fixed in one pass:
- MAXSTUDIOBONES macro redefinition (prefix default + permutation define)
  killed all 373 studio permutations — the "invisible characters".
- sampler2DArray in the unguarded BMODEL_MULTI_LAYERS terrain blocks
  (specular.h, emboss.h, terrain.h, both bmodel fps) can never compile
  under ES 1.00 — part of the invisible ground. GL 2.1 now falls back to
  the single-texture terrain path.
- InteriorMapping( ..., 0, ... ), (1 - diffuse.a), mod(x, 4), <= 50,
  M_PI/180 — int literals in float contexts again.
- glitch_fp used uvec3 hashing (GLSL 1.30+) — float hash33 now.
- shadow sampling used GLSL 1.30 shadow samplers; under GL4ES the depth
  maps are sampled as regular textures and compared with step() — real
  filtering (Vogel disk, PCF) preserved.
- SMAA/Glitch needed round()/textureLod/textureLodOffset shims.

Perf: gl_mobile_quality preset (default on GL 2.x) turns off the
per-frame effects the heartbeat showed costing the most.

## 20. v0.2.4: the yalnie merge

A second Android port existed all along: yalnie/Diffusion-Android (the dev
reached out and shared the source). Same architecture — Diffusion game code
on xash3d-fwgs via gl4es — but the opposite shader strategy, and his is the
one the device likes:

- We downgraded to #version 120 (v0.1.4) and fought ES 1.00 strictness for
  three rounds (int literals, shadow samplers, texture arrays, round(),
  textureLodOffset...).
- He kept #version 130: gl4es translates 130 to ES 3.00 on the Mali-G57,
  where all of those constructs exist natively. His remaining problems were
  source-level and small (case sensitivity on Android FS, int/float
  mismatches), all fixed in his tree.

Takeaway recorded for the future: on GLES3-class hardware, prefer the
gl4es 130->ES3.00 path over downgrading the GLSL version.

Merged his client/server/common/game_shared/glsl (558 files), kept our
engine and build integration, re-applied our two render-layer fixes he
lacked (MAXSTUDIOBONES redefinition guard, gl_mobile_quality preset).
New offline sweep: scripts/validate_gl130.py — all 384 permutations
compile clean under desktop GLSL 1.30 (the grammar gl4es receives).

## 21. v0.2.5: first post-merge device log — stable at last, now perf

The v0.2.4 log is a milestone: zero GLSL errors, every shader compiles,
graphics STABLE. Remaining issues from the log:

1. 7 fps with no heartbeat lines — the merge had wiped our r_main.cpp
   diagnostics. Restored. The preset also missed two heavy defaults in
   yalnie's tree: r_grass and r_grass_lighting default 1 (a grass
   geometry pass every frame). Preset v2 turns both off and switches the
   0.85 renderscale upscale from bilinear to FSR1 (his tree ships
   EASU/RCAS; the shimmering is almost certainly the bilinear upscale).
2. Audio glitching mechanism CONFIRMED by code: engine mixes
   _snd_mixahead 0.12 s ahead, but 7-fps frames take ~0.14 s — the DMA
   buffer underruns every single frame. Android default now 0.3.
3. "Some textures won't load": no texture errors logged — suspect the
   terrain multi-layer path falling back when gl4es doesn't expose
   GL_EXT_texture_array. Ref now prints the caps line at init; the next
   log settles it. (Missing car models + HL1 sounds remain mod content,
   not the port.)

## 22. v0.2.6: the v0.2.5 regression, root-caused twice over

The v0.2.5 heartbeat (back after the merge wiped it) earned its keep
immediately: menu 0.2-2.3 fps (2170-6014 ms frames), in-game 4.8-6.8
fps at 100k epoly, zero shader errors, and the caps line printed
'texture_array no'. Two root causes fell out:

1. FSR was the v0.2.5 regression. FSR() runs whenever gl_fsr>0
   regardless of renderscale (menu renders at 1.0!) and does a
   pglCopyTexSubImage2D fullscreen copy every frame — through gl4es
   that is a CPU readback: seconds per frame on Mali, and the broken
   composite painted checkers/purple and garbage glass. Preset v3 sets
   gl_fsr 0; grass toggles reverted to yalnie defaults (their cost was
   negligible next to the FSR cliff).
2. The purple terrain checkers were PRE-EXISTING (v0.2.4 'some
   textures won't load'): LoadTerrainLayers calls LOAD_TEXTURE_ARRAY
   unconditionally; without GL_EXT_texture_array the ref downgrades the
   upload to a GL_NONE stub; the terrain shader (no
   GLSL_ALLOW_TEXTURE_ARRAY, texfetch.h maps sampler2DArray->sampler2D)
   samples the stub. Fixed: 2D single-layer fallback when arrays are
   unsupported — terrain now renders base-layer through the blend masks.

Steady-state in-game cost is still ~2x our pre-merge tree at equal
epoly (146-208 ms vs 59-83 ms at 100k) — that gap is the next hunt
(lightprobes? particles? studio uniform traffic?) once this build is
confirmed stable. Audio underruns at 6 fps are expected to shrink with
the FSR cliff gone; _snd_mixahead stays 0.3.
