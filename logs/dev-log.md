# Development Log

## 2026-11-20 — Project bootstrap

- Created repository `half-D` (public): research & planning phase.
- Completed Phase 0 research (see `logs/research-log.md`, `docs/RESEARCH.md`).
- Wrote plan documents: roadmap, build plan, controls plan, device notes, permissions.
- Added tools: `check_build_env.sh`, `capture_logcat.sh`, `bench_summary.py`.
- Generated Phase 0 PDF report: `deliverables/half-D_Phase0_Report.pdf`.

### Next session (Phase 1 kickoff)
1. Get written permission confirmation from Aynekko (Q3).
2. `git clone https://github.com/FWGS/xash3d-fwgs engine/` and identify the commit Diffusion targets (Q1).
3. Install NDK/SDK, run `tools/check_build_env.sh`, build stock APK, first run on device.

## 2026-11-20 — Phase R kickoff: vendored the fork, first Android CI

- Inspected Aynekko/Diffusion source: custom client renderer (Uncle Mike,
  GPL-3.0) with ~50 desktop GLSL 1.20 shaders — including built-in FSR1
  (fsr_easu/rcas) — PhysX 4.1 server physics, separate MainUI + launcher repos.
- Vendored into monorepo at pinned commits (see */UPSTREAM.md): engine
  11ed246c, game caaa327c, mainui 33dab31, game_launch 421e1812.
- Engine submodules hand-pinned (gitlab bzip2 unreachable → system libbz2;
  update=none submodules cloned manually: nanogl cf7ccf7, gl4es a025615b).
- Patched game/wscript: added xshlib+xcompile (Android cross-compile) and
  android-conditional subprojects. First half-D fork change.
- Added scripts/fetch_physx.sh + .github/workflows/build-apk.yml (engine APK +
  game-libs jobs). Dispatched first CI run — expect iteration.
- Docs: PORTING_NOTES.md (the "different components" answer), WHAT_TO_DOWNLOAD.md.

## 2026-11-20 — Phase R milestone: first green build (M0.5 "CI produces APK + libs")

CI iteration trail (10 runs, every fix a real Android-porting lesson):
1. ffmpeg prebuilt naming: get_ffmpeg_archive() needs GH_CPU_OS=android (assets are
   androidarm32/androidarm64).
2. Diffusion waf lacked android cross-compile → vendored engine xcompile +
   host_tool_compiler into game/scripts/waifulib; subprojects conditional.
3. PhysX on android: unix sources are PX_ANDROID-guarded but nothing defined it →
   server/wscript now adds PX_ANDROID=1 ANDROID=1 on android.
4. game_shared stringlib.cpp uses 'register' → project gnu++14.
5. NDK libstdc++ alias doesn't define __libcpp_verbose_abort → xcompile android
   now links -static-libstdc++ (NDK-recommended, .so self-contained).
6. bionic has no libpthread → dropped -lpthread on android; added -llog +
   vendored NDK cpufeatures (cpu-features.c) for PhysX's android_getCpuCount.
7. engine/.gitignore swallowed 3rdparty/mainui/CMakeLists.txt → force-added.
8. configure-ninja.py: skipped hlsdk-portable (not needed for Diffusion), fail
   loudly with full waf output; engine mainui submodule vendored (8981066);
   bundled bzip2 vendored from libarchive mirror (gitlab unreachable).
9. APK ABIs restricted to arm64-v8a + armeabi-v7a (x86 had no FFmpeg prebuilt and
   no target device).

Result: run 44765503520 all-green. Artifacts:
- half-D-engine-apk (23.5 MB) — forked engine APK (GLES backends: ref_gles1/ref_gl4es)
- diffusion-android-libs (22.4 MB) — client.so/server.so (PhysX)/menu.so + glsl/
  for arm64-v8a

Next: device install test (stock content paths), then Phase R1 shader
conversion (desktop GLSL 1.20 → GLSL ES 3.00) to light up Diffusion's custom
renderer + its built-in FSR1 on the Mali-G57.

## 2026-11-20 — v0.1.0 released; arch clarification; local wipe note

- Sandbox recycled and wiped the local half-d tree mid-turn; everything was
  safe on GitHub — re-cloned (remote is the source of truth, as designed).
- Published GitHub Release v0.1.0 (prerelease): half-D-v0.1.0.apk (engine,
  arm64-v8a + armeabi-v7a) + diffusion-android-aarch64-libs.zip (client 20 MB,
  server 73 MB w/ PhysX, menu 5.5 MB + glsl/).
- Fixed stale docs: README status line, engine/UPSTREAM.md bzip2 note (vendored
  from libarchive mirror), WHAT_TO_DOWNLOAD.md now points to Releases and
  explains amd64 vs arm64: the itch download ships PC binaries (Win x86/x64,
  Linux amd64); the phone is ARM — binaries are recompiled from the GPL source,
  only arch-independent content carries over.

## 2026-11-20 (later) — v0.1.0 device test failed: lib naming; fix in v0.1.1

- User ran v0.1.0 on the Hot 50 4G (log in logs/device-tests/): engine booted,
  found the game folder, init'd video (Mali-G57, GLES1 renderer) — then
  dlopen failures for server/client/menu: our zip shipped
  `client_android_arm64.so` but the engine requests `libclient_android_arm64.so`
  (library_suffix adds the `lib` prefix on Android; FS lookup missed → bare-name
  fallback searched the APK → nothing there either).
- Root-caused in engine source (lib_android.c ANDROID_LoadLibrary chain,
  lib_common.c, library_suffix.c); wrote up in PORTING_NOTES.md §7.
- Fix: game-libs job renames outputs to lib-prefixed names; engine-apk job now
  `needs: game-libs` and bakes the three libs into jniLibs/arm64-v8a (menu as
  libmenu.so = the engine's bare fallback name). APK self-contained now.
- Docs updated (WHAT_TO_DOWNLOAD.md: v0.1.1 flow, lib names; PORTING_NOTES §7).
## 2026-11-21 — v0.1.1 device test round 2: game code runs; renderer is the wall

- Log (logs/device-tests/): server DLL loaded ("Dll loaded for game Diffusion
  1.21"), all engine extensions active, client DLL loaded, video/audio/touch
  fine — then `Host_ErrorInit: This game requires the OpenGL renderer` at
  client R_Init. Secondary crash: PhysX PxCloseExtensions during error
  shutdown (physics never initialized → no foundation).
- Renderer research (source + web) written up in PORTING_NOTES.md §8: needs
  desktop GL compat context, GL 2.0 + VBO/VAO/shader_objects, GLSL 110 (all 74
  shaders), ref_gl4es reports CONTEXT_TYPE_GL and implements VAOs → chosen as
  immediate route; native GLES3 port = Phase R1 unchanged. Aynekko has no
  Android work anywhere; PrimeXT Android is aspirational only.
- v0.1.2 changes: launcher default argv `-ref gl4es`; PhysX FreePhysic null
  guard; FiraSans-Regular.ttf (OFL) bundled into the libs zip.

## 2026-11-21 — v0.1.2 device log: launcher argv bug; gl4es forced at engine level

- v0.1.2 device test: still gles1 + blank screen. The APK had the patch, but
  Game.kt always passes a non-null argv built from prefs, so the XashActivity
  default (with -ref gl4es) never fired; autodetect order starts at gles1 and
  video.cfg now also carries a saved r_refdll gles1. Full chain in
  PORTING_NOTES.md §9.
- v0.1.3: gl4es forced in ref_common.c on Android (after explicit -ref, before
  r_refdll), Game.kt + XashActivity defaults, and the menu font extracted from
  APK assets into <rodir>/<gamedir>/gfx/fonts/ at launch.
- Also: recurring workspace-restore glitch hit again between turns (local tree
  reverted to fb6f94d with edits uncommitted) — recovered by rescuing edits,
  hard-syncing to remote main, re-applying; pushed fast-forward on top.

## 2026-11-21 — v0.1.3 device log: shaders demanded #version 130 on a GL 2.1 context

- v0.1.3 device test: gl4es finally loaded and the renderer context check
  passed; crash moved to assert r_shader.cpp:1546 (GenSunShafts NULL).
- Root cause: GL_ProcessShader hardcodes #version 130; gl4es = GL 2.1 →
  every shader fails to compile. Errors invisible because ALERT() is a no-op
  without -dev (our default argv had none).
- v0.1.4 (game fork): version picked from glConfig.version (130/120/110),
  texture()→texture2D define below GL 3.0, unconditional failure logging,
  -dev 2 in launcher defaults, 34 guarded uniform inits so a failed optional
  effect shader skips instead of fatal. Residual GLSL-130-only effects
  (SMAA/FSR/glitch/emboss via textureLod/textureSize) documented as off.

## 2026-11-21 — v0.1.4 device crash: map-load OOB read; mathlib.h broke every shader under ES 1.00

- v0.1.4 booted to menu and started map load; SIGSEGV at Mod_LinkLeafLights
  r_world.cpp:760 — one-past-end deref of the leaflights array after all
  lights are consumed (guard page on Android). Guarded with j < numleaflights.
- Shader failures in the same log had no error text (ALERT/Msg developer
  gated). Perfect correlation: failed == #include mathlib.h. Root cause:
  desktop-only `const in` parameter qualifiers (12 sites across 6 shader
  headers) + implicit int->float conversions in mathlib.h — both invalid in
  GLSL ES 1.00, which is what gl4es feeds the Mali driver.
- v0.1.5: OOB guard, const-in sweep, mathlib float casts, unconditional
  shader compile/link diagnostics (driver info log now always in engine.log).

## 2026-11-21 — v0.1.5 device crash: desktop-generated .tbn studio caches

- Screenshot-only report: assert r_studio.cpp:1733 (TBN sanity in
  StudioCreateMeshCache). The itch package ships cache/*.tbn files from
  the author's Windows build; they pass ident/version/CRC but their
  submodel vertex offsets don't match this build → assert at map load.
- Workspace glitch struck again: half-d tree was reset to the v0.1.0-era
  commit fb6f94d with all v0.1.1-v0.1.5 work gone locally. Everything was
  pushed though — recovered via fetch+reset to d91c784, re-applied the
  v0.1.6 patch cleanly.
- v0.1.6 (game fork): StudioLoadTBN always returns false (regenerate TBN
  in-process), dml/TBN sanity checks downgraded to warnings, missing
  j != MAXSTUDIOMODELS bounds guard added before dml->submodels[j].

## 2026-11-21 — round 7 report: wrong log attached (no half-D data)

User reported "crashed again" but attached mh4u-log.zip — a diagnostic from
a different app (com.mh4u.native, Monster Hunter 4 Ultimate port, v1.0.1.1)
on a different phone (Infinix X6882, Helio G99), captured 2026-11-20 08:43
(before v0.1.5 even shipped). No Xash/Diffusion strings, no crash markers —
the app exits in it are clean swipe-ups. Nothing to diagnose from it; asked
for the half-D crash zip (auto-packages engine.log) or the engine.log file.
v0.1.6 remains the current release awaiting a real report.

## 2026-11-21 — v0.1.6 device crash: game renders! glsl never reached the device

- Crash stack: HUD_RenderFrame → R_DrawWorld → R_DrawBrushList →
  R_DrawGrass r_grass.cpp:1704, SEGV at 0x524. Map load + studio models
  all passed — the render loop is LIVE.
- engine.log (76KB) with unconditional diagnostics: Mali driver says
  "S0001: Type mismatch int/float" — but repo mathlib.h is clean since
  v0.1.5, and the failure list is IDENTICAL to v0.1.4 → the device
  compiled the ORIGINAL itch glsl from the game data folder. The APK has
  no glsl and the FS search order (game dir > APK assets) shadowed all
  our shader fixes. The grass crash is NULL genericFog → currentshader
  NULL → u_FogParams at +0x524 — matches the fault address exactly.
- v0.1.7: engine FS_Rescan mounts APK assets again as highest-priority
  override; patched glsl ships in APK assets (CI step); android.c honors
  mount path as filename (dedupe bypass); grass renderer guards both
  currentshader derefs.

## 2026-11-21 — v0.1.7 device crash: world renders, bloom kills the driver

- Crash: Bloom() r_postprocess.cpp:1065 -> glCopyTexSubImage2D -> gl4es
  texture_read.c:165 readback -> gl4es_glTexSubImage2D texture.c:1755
  software-mipmap chain -> Mali memcpy SEGV_ACCERR. Root cause: NPOT
  screen texture with mip chain + mip FBOs; GLES2 can't glGenerateMipmap
  NPOT -> levels unallocated -> driver faults uploading into them.
- Override confirmed working: failures 27 -> 10. GenLuminance now the
  only bloom-chain shader that failed... and ToneMap (gl_exposure=1
  default) would have crashed right after Bloom with the same pattern.
- Remaining 10 failures decoded from the driver text: 8 trivial ES1.00
  violations (fixed), Glitch+SMAA are GLSL 130-only (guarded, stay off).
- v0.1.8: 8 glsl fixes, Bloom/ToneMap GL3 gate, CheckShader guards on all
  post effects (MotionBlur was called unconditionally every frame!).

## 2026-11-21 — v0.1.8 device: game loads; three birds with one log

- Repo recovery first: the local clone was a stale Nov-20 snapshot and all
  v0.1.1..v0.1.8 commits lived only on origin/main; reset to FETCH_HEAD
  (verified tree == 2381f4c) before continuing.
- Mixed-case shader names (GrassDepth, Bmodel*, Studio*) missed the APK
  asset override (AAssetManager_open is case-sensitive) -> compiled the
  unpatched game-folder glsl -> 131 failures/session, surfaces skipped.
  Fixed with a case-insensitive retry in FS_FindFile_AndroidAssets.
- emboss.h/alpha2coverage.h need textureSize()/array ctors (GLSL 120/130)
  -> GL4ES-guarded no-op stubs; terrain stays GL3-only (texture arrays).
- 0-1 fps: sun shafts do fullscreen color+depth copies every frame; under
  GLES2 color=readback stall, depth=guaranteed failure+garbage. GL3 gate.
- Controls: touch engine is fine (touch_enable=1 on android); the APK just
  shipped no touch button art. Generated a 31-icon touch_default pack +
  stick_thumb + numbers.cfg into APK assets.

## 2026-11-21 — v0.2.0: found it, then actually delivered it

- Sandbox restored the stale Nov-20 clone again; fetch+reset to
  origin/main (73ed5a6) before touching anything. Always check git log
  first now.
- v0.1.9 regression: the case-insensitive APK fallback returned the
  mixed-case ORIGINAL in fixedname, so the open failed -> 169 'couldn't
  load' per session -> all bmodels/studio models invisible (the 'sunset'
  was the skybox behind empty space). Now returns the corrected name.
- The real fps killer: screen textures were GL_RGB8, and gl4es only
  passes glCopyTexSubImage2D to the driver for RGBA/UBYTE - every post
  effect did a 4MB CPU readback per frame. TF_SCREEN |= TF_HAS_ALPHA
  makes them RGBA: native GPU copies, effects stay on.

## 2026-11-21 — v0.2.1: three '.0' suffixes and the instrument we needed

- v0.2.0 device log: 10 fps (up from 4), trees/cars/characters visible,
  cutscenes render. Ground still missing: BmodelSolid/BmodelDlight fail
  'S0001 int->float' on every permutation — all carry BMODEL_INTERIOR
  or BMODEL_WATER. Root cause: int-literal float consts (staticSeed=500,
  u_zFar=-4096 x2). Strict ES 1.00 has no implicit conversions; desktop
  120 does. Fixed with float literals.
- Dips: failed programs wiped without hash registration -> every request
  re-compiled on the driver. Added the negative cache.
- Built the diagnostics layer (user request): compile trace, 5s perf
  heartbeat, >150ms spike lines, gl_diag cvar default-on.
- Syntax-checked r_shader.cpp/r_main.cpp locally with g++ -fsyntax-only
  (clean) before pushing to CI.

## 2026-11-21 — v0.2.2: 26 ones and a stopwatch

- v0.2.1 device log: perf heartbeat is in and instantly useful. Fps
  now legible: epoly-bound (12-17 fps @ 80-100k studio polys; 49 fps
  idle). Negcache effective (0 retries). Two 32-36 s single-frame
  spikes = chapter loads.
- Interior shaders: staticSeed fix moved the failure one bug deeper to
  randomFunction2( 1, ... ) x26 — int literal for a float param; ES 1.00
  overload resolution does no implicit conversions. Fixed + 2 mix(0,).
- Engine: load-stage timing in SV_SpawnServer/SV_ActivateServer
  (world, submodels, game activate, settle frames) to attribute the
  36 s loads next run.

## 2026-11-21 — v0.2.3: 384 permutations, 0 failures

- Sandbox restore had rolled the repo back to v0.1.0; origin/main had
  everything (user: "all progress there"). Hard-reset to 42fc5d9.
- Built scripts/validate_es100.py: glslang 16.6.0 + ref-preprocessing
  replication + gl4es translation emulation. Found and fixed every
  remaining ES 1.00 violation across all 384 permutations in ONE round
  instead of one bug per device-test round. Final: 0 failing.
- The validator harness itself needed three emulations: precision
  injection, f-suffix stripping, legacy GL matrix stubs.
- Mobile quality preset (gl_mobile_quality) on the GL 2.x path.

## 2026-11-21 — v0.2.4: the yalnie merge

- yalnie (Diffusion-Android dev) shared his source: same gl4es
  architecture, opposite shader strategy (#version 130, gl4es -> ES 3.00).
  Our v0.2.1-v0.2.3 ES 1.00 fight was the wrong hill — 130 keeps round(),
  textureLodOffset, shadow samplers, texture arrays alive on Mali-G57.
- Merged his game tree (558 files: client/server/common/game_shared/glsl)
  into game/, kept our engine/build/CI, re-applied MAXSTUDIOBONES guard +
  mobile quality preset on top of his files.
- validate_gl130.py sweep: 384/384 permutations clean under desktop 130.

## 2026-11-21 — v0.2.5: post-merge fix round

- v0.2.4 device log: graphics stable, shaders clean, preset applied.
  7 fps + audio glitch + shimmer + some missing textures.
- Audio = _snd_mixahead 0.12 < 140ms frames -> per-frame underruns.
  Android default now 0.3.
- Heartbeat restored (merge wiped it); preset v2: r_grass 0,
  r_grass_lighting 0 (defaults 1 in his tree — a per-frame grass pass the
  preset never covered), gl_fsr 1 for the 0.85 upscale (vs bilinear
  shimmer).
- GL caps line added at ref init (texture_array etc.) to attribute the
  missing-texture/terrain-layer question next log.

## 2026-11-21 — v0.2.6: stable build

- v0.2.5 broke the game: purple checkers, broken glass, menu at
  0.2-2.3fps. Heartbeat data + code walk = two causes:
  (a) FSR(): runs at ANY renderscale, pglCopyTexSubImage2D per frame =
  gl4es CPU readback = seconds/frame + broken composite. Preset now
  gl_fsr 0.
  (b) LoadTerrainLayers unconditional LOAD_TEXTURE_ARRAY -> ref
  downgrades to GL_NONE stub -> purple checkers. Fixed with 2D
  single-layer fallback (pre-existing since v0.2.4).
- Grass toggles reverted (no measurable cost).
- Keep: heartbeat, GL caps line, _snd_mixahead 0.3.

## 2026-11-21 — v0.2.7: DownScale was the second readback

- v0.2.6 verdict: sounds fixed, fps not. Code walk: DownScale() at
  renderscale<1 = same pglCopyTexSubImage2D per frame as FSR. Preset v4
  sets renderscale 1.0. 16bpp copy also suspect for glitchy look.
- Heartbeat now reports per-stage ms (scene/postfx/anim/cube).
- Ref logs texture misses + upload failures by name; caps2 line for
  s3tc/etc1/astc/fbo.
- Sandbox recycle mid-round restored a v0.1.0-era tree; reset to
  origin/main recovered everything (v0.2.x lives on GitHub).

## 2026-11-21 — v0.3.0: rebase onto yalnie's Diffusion-Android

- Research: no Aynekko engine fork (stock fwgs + client renderer);
  yalnie/Diffusion-Android = the working Android port; our v0.2.4
  Aynekko-master merge had dropped his GLES/ARM renderer fixes
  (exposure/tone-mapping, SMAA formats, 76-bone Mali cap, signed
  boneid, GLSL strictness, BSP alignment).
- game/ vendored from yalnie b63a4cf; our patches re-applied (preset
  v5, heartbeat+stages, terrain fallback, DownScale warning, waf
  --android flow); our ES1.00 rewrite dropped for his #version 130.
- Engine: +4 yalnie commits (GLES3 ctx/GL2 shim, levelshot, BSP
  realign x2); +--enable-gles3compat/--enable-gles1.
- Sandbox recycled twice mid-round; both times recovered from
  origin/main. All v0.2.x work lives on GitHub.

## 2026-11-21 — v0.4.0: the log finally talks (menu crash, dead lights, shader cache)

v0.3.0 device log + code walk pinned four independent mechanisms:

1. Menu/options crash (device reset): GaussBlur() has an UNGATED
   "blur when paused (menu shown)" path — RenderGaussBlur does TWO
   fullscreen pglCopyTexSubImage2D per frame while any menu is open
   (r_blur 0 only gates water/sky/damage blur). Same gl4es CPU-readback
   cliff as FSR/DownScale: 0.5-13 s frames -> GPU watchdog. FIX:
   r_menu_blur cvar, 0 in mobile preset v6. Audited every other copy
   path — all properly gated.
2. Lighting dead: no GL_ARB_depth_texture via gl4es -> TF_SHADOW stubs
   return 0 (engine turns TF_DEPTHMAP into GL_NONE) -> every dlight
   pass aborts ("shadow texture for the light is missing") -> zero
   dynamic light contribution. FIX: white 8x8 plain stubs for
   depthTexture/depthCubemap (shadowless lights — exactly what the
   code comment demanded), FBO texnum-0 guard, one-shot diagnostics.
3. 1-13 s spikes, no precompile: binaryshaderfuncs required
   glProgramParameteri(ARB) in the all-or-nothing table; gl4es exports
   the binary get/set but NOT the hint setter -> R_BINARY_SHADER_EXT
   off -> no .shadercache disk cache, no progressive precompile, lazy
   mid-frame compiles. Table also clobbered pglGetProgramivARB
   (copy-paste). FIX: table = 2 required entry points, hint setter
   resolved separately + NULL-guarded at 3 call sites. First launch
   compiles ~1100 permutations behind MainUI's compile screen, then
   loads from .shadercache every launch.
4. Console font: MainUI maps Tahoma -> gfx/fonts/tahoma.ttf which the
   itch package does not ship (log: "Unable to read font file").
   FIX: map to the bundled FiraSans (SIL OFL).

Diagnostics for round 18: BSP load-stage timers (25 s world load),
caps2 += bptc/rgtc/program_binary (raindrop.dds upload failure =
likely BC7 without BPTC), upload-fail message += pixel format name.

Open: menu bg map still renders 282k epoly studio geometry (~100 ms
scene at 11 fps in-game is playable; menu usable once blur is gone);
fleck/glass_gib/sky/lambda = missing mod content.

## 2026-11-21 — v0.4.1: the crash was the options UI re-arming the readbacks

v0.4.0 device log verdict: all four fixes verified live (no shadow-texture
errors, FiraSans loaded, program_binary yes, precompile ran). But the
crash survived, load still 25 s, and "Loaded 832 shaders: 279 failed"
with zero error output.

- CRASH ROOT CAUSE: the video options presets (menus/VideoOptions.cpp)
  set r_bloom 1, gl_sunshafts 1, r_blur 1, gl_hbao 1, gl_water_refraction
  1... — each re-enables a per-frame fullscreen pglCopyTexSubImage2D
  readback that the mobile preset had turned off once at init. Selecting
  any graphics option re-arms the gl4es readback cliff -> multi-second
  frames -> GPU watchdog kills game and device. FIX: one hard gate
  (R_AllowScreenCaptureFX) at the top of all 11 capture functions
  (sunshafts, motionblur, gauss, hblur, screenwater, glitch, monochrome,
  ssao, bloom, FSR, downscale). On the GL2 path they stay off no matter
  what the cvars say; gl_mobile_quality 0 opts back in. r_menu_blur from
  v0.4.0 becomes redundant but stays.
- 279 shader failures were silent because ALERT() is a no-op without
  developer mode. Code analysis: 169 HAS_SHADOWS + 196 SUN_SHADOW
  permutations use sampler2DShadow/shadow2DOffset, which gl4es cannot
  translate (the GL_ES fallbacks in shadow_*.h don't apply — gl4es
  presents desktop GL 2.1, so GL_ES is never defined). Failed
  permutations are not hashed, so runtime requests retried a full driver
  compile every frame. FIXES: negative cache (failures remembered, never
  retried this session), always-on throttled failure logging with
  name/options/stage, and a precompile filter that skips the ~413
  device-impossible entries (shadow-sampler + texture-array defines).
- 25 s world load: the heap-stage timers from v0.4.0 printed NOTHING,
  so the thief is before them. Now timed: studio-cache purge, FS file
  read (with MB), lump byte-swap, FindBSPX, raw lump loop.
- raindrop.dds is BC7 SRGB (device-confirmed) — Mali has no BPTC via
  gl4es. Shipped an uncompressed BGRA replacement (32x128 procedural
  streak, tools/make_raindrop.py) in the libs zip.
- Heartbeat now reports dips/passes/binds to tell draw-call-bound from
  geometry-bound in the ~100 ms scene stage.

## 2026-11-22 — v0.4.2: the v0.4.1 crash was my own raindrop.dds

Device logs (two crashes, both within 5 s of launch, before the menu):
the second one carried the smoking gun —

    Mem_FreeBlockBig: trashed header sentinel 2
    (alloc at engine/common/imagelib/img_dds.c:420, check at img_main.c:596)

Both backtraces (SEGV in libft2/hwui text shaping; SEGV in
android::RefBase::incStrong) were collateral of a corrupted heap, not game
code. Root cause: my uncompressed raindrop.dds set the DDSD_PITCH flag,
and Image_DXTCalcSize's PITCH|LINEARSIZE branch uses dwPitchOrLinearSize
as the TOTAL size — I had written the per-row pitch (W*4 = 128) there, so
the loader allocated a 128-byte buffer for a 16384-byte texture and the
upload path wrote 16 KB through it.

Fixes:
- generator: DDSD_LINEARSIZE with the full W*H*4 (16384 == file bytes ==
  loader expectation, verified by replicating Image_DXTCalcSize's branch
  logic against the new header).
- engine hardening: in the PITCH|LINEARSIZE branch, a PITCH-flagged
  UNCOMPRESSED file now computes pitch * height instead of trusting the
  field, so no tool-generated file can misallocate the buffer again.

Everything else in v0.4.1 (capture-FX gate, negative shader cache,
precompile filter, load timing) never got to run — the crash happened
during client texture init. All of it carries into v0.4.2 unchanged.

## 2026-11-22 — v0.4.3: the v0.4.2 crash = stale FS pool after in-process restart

Device log (crash 01:03:26, build 1b7069b = v0.4.2): SEGV at 0x100 in
FS_AddZip_Fullpath (zip.c:735) during FS_Rescan, preceded by
"Mem_FindPool: not allocated or double freed pool 5" — with zero game
frames run. Not my raindrop this time and not the game code at all.

Mechanism, reconstructed from source:
1. Memory_Init() at the top of every Host_InitCommon frees the ENTIRE
   pool table and resets poolcount = 0 (in-process restart support).
2. The engine was restarted within the same Android process (error
   dialog / activity relaunch — the exact first error is unknown
   because Sys_InitLog re-truncates engine.log in "w+" mode on every
   non-changegame restart, erasing the previous lifetime's log).
3. The relaunched engine ran on with the filesystem library still
   holding fs_mempool = 5 from the previous lifetime — a handle past
   the end of the fresh, smaller pool table.
4. Mem_FindPool hit its "not allocated or double freed pool" path.
   That Sys_Error is survivable-but-lethal: Host_ShutdownWithReason
   early-outs (shutdown_issued is never reset after a restart), the
   longjmp never fires, execution CONTINUES past the "fatal" error.
5. Mem_Calloc returned NULL, FS_AddZip_Fullpath dereferenced the NULL
   searchpath → SEGV at 0x100.

Fix bundle (all defensive, all in the engine):
- zone.c: Mem_FindPool no longer Sys_Errors on an invalid handle —
  loud Con_Printf with a newline, return NULL, callers degrade.
- filesystem.c FS_InitMemory: probe-validate the inherited pool handle
  and take a fresh pool when stale (self-heal across restarts).
- zip.c / dir.c / searchpath.c: every searchpath allocation is checked;
  a failed mount now skips that archive/directory with a clear error
  line naming the file instead of crashing.
- host.c: reset host.shutdown_issued at init so a restarted engine can
  actually shut down on its next fatal error.
- sys_con.c: engine.log APPENDS when the engine restarts in-process,
  so the next crash report will finally contain the ORIGINAL first
  error instead of a truncated log.

## 2026-11-22 — v0.4.4: the crash was MY v0.4.0 shader binary cache; gl4es was never stale

Crash 02:04:32 (build 35d7937): SEGV at 0x20 in gl4es fpe_CustomShader
(fpe.c:468 = glprogram->last_vert->source) while drawing the menu world
(maps/menux) on the Infinix X6882 (Helio G99 / Mali-G57). The game RAN —
v0.4.3's boot fixes all held; this is the in-game phase.

Forensics against the reference port (yalnie/Diffusion-Android):
- My vendored gl4es is content-identical to yalnie's pin (ptitSeb a025615b,
  2026-10-14). The "1.1.6" CHANGELOG head misled me — upstream stopped
  updating the changelog; version.h says 1.1.7. A re-vendor produced a
  268-file diff with ZERO insertions/deletions. gl4es was never the delta.
- My engine already contains both of yalnie's fork commits (gles3compat
  context + GL2_ShimGetProcAddress; cl_scrn levelshot guard + V_PostRender
  transition fix). Engine = reference too.
- The remaining delta from the working reference was MY client patch stack.

Root cause: v0.4.0 made the GL_ARB_get_program_binary table resolve without
glProgramParameteri "because gl4es doesn't export it". That ENABLED the
shader disk cache on gl4es: programs restored via glProgramBinary have no
gl4es-tracked shader sources, so program_t->last_vert is NULL. The first
fixed-function state change on such a bound program (fog/alpha in the menu
world) sends gl4es into fpe_CustomShader -> last_vert->source -> SEGV.
The same source-less programs explain visual glitches: fixed-function state
(fog, alpha test) that gl4es should bake into customized variants simply
never gets applied to them.

Fixes:
- r_opengl.cpp: binaryshaderfuncs requires glProgramParameteri(ARB) again
  (pointer bug stays fixed) — gl_binaryshader is now DISABLED on gl4es, all
  programs compile from source = reference behavior. The game's precompile
  pass is tied to the binary cache and skips itself (as on the reference).
- gl4es fpe.c: fpe_CustomShader / fpe_CustomShader_DefaultVertex guard
  against source-less programs and cache allocation failure — fall back to
  the plain program instead of crashing (upstream still lacks this).
- assets/particles: 4 placeholder DDS (fleck_wood1, fleck_dirt, glass_gib,
  tile_gib) for textures referenced by particles.txt but absent from the
  1.21 data — debris now renders instead of missing-texture checkers.
- CI packs every assets/particles/*.dds into the libs zip.

Kept (protective or diagnostic, none affect the crash path): capture-FX
gating (options-menu readback cliff), dlight shadowless stubs, shader
failure logging + negative cache (failed-only), mobile quality preset,
engine lifecycle fixes from v0.4.3.

Documented for the user: the APK also ships libref_gles3compat.so — if
gl4es still misbehaves on this device, "gles3compat" can be tested via
-ref on the command line.

## 2026-11-22 — v0.5.0: native GLES 3 renderer port (the "heavily modified" round)

The v0.4.4 field log settled it: menus no longer crash, but 5-15 fps with
80-160 ms steady scene times, spikes to 66-76 s, and render scale changes
change nothing — the frame is CPU-bound inside the gl4es translation layer,
not fill-bound. Aynekko's engine link resolved to what we already ship (fwgs
+ their game code; there is no separate engine fork), so the remaining lever
was the renderer path itself: run the client's GL2 renderer NATIVELY on the
GLES 3.2 context (Mali-G57) instead of through gl4es's per-call translation.

Port work (client, r_opengl/r_shader):
- ES context detection + "OpenGL ES x.y" version parse (Q_atof read 0)
- GLSL ES 300 on-the-fly transform in GL_ProcessShader/GL_ParseFile:
  #version 300 es, texture2D/Lod/Proj/Cube(+Lod) -> texture/textureLod/
  textureProj, gl_FragColor -> declared out, varying -> in/out, attribute
  -> in, gl_ModelViewProjectionMatrix/gl_ModelViewMatrix/gl_TextureMatrix[0]
  -> u_hdMVP/u_hdMV/u_hdTexMat0 uniforms (uploaded in GL_BindShader),
  gl_Vertex/gl_Normal/gl_Color/gl_MultiTexCoord0 -> layout-pinned shim
  attribs, gl_TexCoord[0] -> declared varying, gl_ClipVertex -> sink,
  "sample" (ES300 reserved word) -> hd_sample
- shadow_*.h: ES 300 branch — shadow samplers are core there; the ES 1.00
  fallbacks' #ifndef guards are false when the types are predefined
- r_opengl GL_CheckExtension: stub table for every resolve-only desktop
  function (call-site audited by tools/audit_es_surface.py), ARB object
  API wrappers (create/attach/delete/use/info/params -> core GL2), generic
  ARB/EXT suffix retry (glGenBuffersARB -> glGenBuffers, shim exports...)

Port work (engine):
- gl2_shim: matrix stack (push/pop/rotate/mult/frustum + double variants),
  missing immediate-mode variants, GL2_GetMatrix export (halfD_GL2GetMatrix),
  dedicated texture-matrix tracking, draw-wrapper + glBindBuffer exports
  (client-memory arrays — grass, decals — must flow through SetupArrays),
  GL_QUADS array expansion via the shared triquads ibo + basevertex
- ref_common.c: Android renderer preference gles3compat first, gl4es as
  automatic fallback; launcher no longer pins -ref gl4es

Precompile:
- decoupled from the binary cache (v0.4.4's crash fix had disabled it —
  every permutation compiled lazily mid-frame = the 13-76 s stalls);
  compiles from source behind the loading screen again

Validation: tools/validate_es300.py replicates the exact preprocessing and
transform, compiles+links every shaderlist permutation and fixed glsl pair
with glslangValidator as GLSL ES 300: 1141/1141 clean (was 1005/1141 at the
first run — found and fixed samplerCubeShadow precision, shadow2D ES300
mapping, sample keyword, texture2DProj, gl_ClipVertex, gl_Vertex...).

Known limitations on the ES path (documented for the field log): studio
decals render through the shim's fixed-function program (attribute
locations differ from the decal shader); shadow permutations compile but
are untested on device; gl4es remains selectable via -ref gl4es.

## 2026-11-22 — v0.5.1: crash fix (first native-GLES3 field run)

The 09:28 crash zip tells a good story: the port itself worked end to end —
"Loading renderer: gles3compat", "GL_VERSION: 3.2 (GLES)", menu world loaded,
map load started. Then signal 11 in V_CalcFirstPersonRefdef (view.cpp:2533):
the weapon-obstacle trace dereferences worldmodel->leafs, and worldmodel was
NULL because "GL_ARB_multitexture is not supported. Custom renderer disabled"
appeared in the log one line after the context came up.

Root cause: GL_CheckExtension requires desktop GL_ARB_*/GL_EXT_* extension
STRINGS before it even tries the function table. Native GLES3 has multitexture
in core and never lists the string, so the check bailed, the custom renderer
disabled itself, worldmodel never got set — and the first first-person view
calculation faulted at NULL+0xa0.

Fix:
- on ES contexts the string precheck is skipped; the function table is the
  source of truth (every table was already made to resolve via shim/stubs/
  ARB-alias in v0.5.0, and ES3 advertises the real capabilities it has —
  shadow_samplers already showed "yes" in this log)
- defensive guard at the crash site: the obstacle trace only runs when
  worldmodel/viewleaf are live, so a future renderer-init failure degrades
  to "no weapon pullback" instead of a segfault

Note for the next log read: with multitexture now on, the client-side
texture-array check should also flip on (ES3 core) — terrain shaders were
validated for that path (776 pairs including terrain, 0 failures).

## 2026-11-22 — v0.5.2: the multitexture veto, round two + engine mesh batch overflow

The 09:55 crash: v0.5.1's es_context skip worked (log printed "3.2 (GLES)")
but "GL_ARB_multitexture is not supported" STILL appeared. The failure moved
one branch deeper in GL_CheckExtension: cvar_from_engine=true makes the client
read the ENGINE's gl_arb_multitexture cvar — the native ES ref never probes
the desktop ARB names, so the cvar is missing or archived 0, and the check
returned "disabled" before looking at the function table that v0.5.0 made
fully resolvable. Meanwhile the engine's own fallback renderer took over,
drew the world fine, and crashed in R_StudioBuildArrayFloatMesh writing
studio mesh batches past the fixed MAXSTUDIOVERTS arrays (high-poly models).

Fixes:
- client: on ES contexts the capability-cvar value is ignored — the function
  table decides (r_extensions master switch still honored)
- client: GL_MAX_TEXTURE_UNITS is ES1 fixed-function and fails on native ES3;
  fall back to GL_MAX_TEXTURE_IMAGE_UNITS like the engine ref does
- engine ref: all three array mesh builders (Normal/Float/Chrome) now
  truncate a batch that would overflow the vertex or element arrays instead
  of writing past them

## 2026-11-22 — v0.5.3: the last shader-object name + the perf watchdog

The 09:55→v0.5.2 log: multitexture passed (v0.5.2 fix confirmed), and the
failure moved to the NEXT gate — GL_ARB_shader_objects. Root cause this
time: glDetachObjectARB. The ARB->core strip gives "glDetachObject" which
is not a function (core name is glDetachShader); it was the ONE member of
the 65-entry shaderobjectsfuncs table with no resolution path, and a single
NULL member disables the whole extension check.

Wrote tools/audit_v053.py, which models the client's GL_CheckExtension
resolution chain exactly as the code runs it (native ES3 | shim exports |
special-case retries | esStubs | ARB/EXT strip) for every name in every
dllfunc_t table. Result before the fix: 273/277, with the four gaps being
glDetachObjectARB, glMultiDrawArrays (really used by the decal batcher,
with GL_POLYGON — a mode ES3 doesn't have), glGetQueryObjectivARB (ES3
only exposes the unsigned read), glTexEnvfv (unused at runtime; the shim
NoOps the scalar texenv calls anyway). All four now have stubs:
DetachObject->pglDetachShader, MultiDrawArrays->per-batch draw with
POLYGON->TRIANGLE_FAN conversion, GetQueryObjectiv->uiv cast, TexEnvfv->NoOp.
Audit now reads 277/277 — every table name in the client resolves on the
native path, so there is no round four hiding in the function tables.

Verified the remaining init gates offline: shading_language_100 and
fragment_shader (NULL tables, predict true on ES), vertex_shader (clean),
FBO + get_program_binary (clean), hardware skinning (both query paths —
COMPONENTS natively, VECTORS fallback — land >= 32 bones on Mali).

Also: low-fps watchdog in R_DiagFrame. When a 5 s heartbeat window averages
under 30 fps it now prints a [watchdog] block: per-stage milliseconds,
dips/binds/polys-per-dip, and a verdict naming the dominant cost (scene
geometry / grass / studio polys / postfx / texture uploads / cubemaps /
draw-call bound / shader-switch bound / not-GPU-bound) plus the concrete
cvars to turn down. All advice cvar names verified to exist in the codebase.

## 2026-11-22 — v0.5.4: the shader prelude emitted a desktop-only #extension on ES

The 11:17 log is the deepest failure yet — and a clean one: the client
renderer INITIALIZED (first time on the native path), started compiling the
real game shaders, and stopped with a host error instead of a segfault:
"0:33: P0001: Extension directive must occur before any non-preprocessor
tokens" on generic_vp (and the same at 0:29 on genshafts_fp), then the
required-shader assert on GenSunShafts.

Line arithmetic pinned it: the only #extension in the emitted source is the
prelude's own "#extension GL_EXT_gpu_shader4 : require" — landing at exactly
line 33 (vp) / 29 (fp) after the uniform/in/out injections. Two faults in
one directive: GL_EXT_gpu_shader4 is desktop-only (ES3 has the integer/
bitwise features in core), and it followed non-preprocessor tokens — which
Mali enforces (P0001) and desktop drivers silently tolerate. The stock game
shaders carry no #extension lines at all (grep-verified).

Why it slipped past the v0.5.0 offline validation: the harness modeled the
gl4es-era device state (gpu_shader4 OFF, texture_array OFF, shadows filtered
out) — but the v0.5.2+ capability fixes made the client predict all three ON
on native ES3, so the device compiled shader permutations the harness had
never validated, with a prelude line it never modeled.

Fixes:
- the gpu_shader4 DIRECTIVE is not emitted on ES contexts (the feature
  defines stay — the shader bodies guard on them and ES3 supports the ops)
- body #extension lines (mod shaders) are commented out on ES — they can
  never be legal after the prelude's declarations
- validate_es300.py now models the REAL native device profile: every
  permutation (1111 shaderlist entries + fixed pairs = 1141 vp/fp compile+
  link), gpu_shader4 and texture_array defines ON, shadow and terrain-layer
  permutations INCLUDED, plus a static check that no #extension ever follows
  a non-preprocessor token in the emitted source
- re-validated with glslangValidator as GLSL ES 300: 1141/1141 ALL CLEAN

## 2026-11-22 — v0.6.0: back to gl4es — the native GLES3 bet didn't pay

The v0.5.4 field report settled it: the custom renderer now RUNS on the
native path but renders wrong (black ground, error models, mesh and lighting
garbage, artifacts), runs 0-4 fps, and opening options/menus crashes the
game AND the device. That is a regression against v0.4.4 (gl4es): correct
visuals, working menus, 5-15 fps.

Forensics against the reference (yalnie/Diffusion-Android, cloned fresh):
its client renderer carries ZERO GLES-specific code — it expects a desktop
GL 2.x context and runs through gl4es, which is exactly xash3d-fwgs's
standard Android story. Our 11-file delta vs the reference is my entire
v0.5.x stack. The experiment — running Diffusion's fixed-function GL2
renderer natively on GLES3 via a text-level shader transform + call shim —
cannot reproduce the renderer faithfully; every visual subsystem would be
its own device-debug round trip.

Changes:
- ref_common.c: Android renderer order swapped back — gl4es first,
  gles3compat the automatic fallback (-ref still overrides). v0.5.0's
  gles3compat-first preference was the only thing routing the device to
  the native path; the build still ships both refs.
- XashActivity: comment updated to match; argv stays unpinned.
- Verified inert under gl4es: every client v0.5.x capability gate keys on
  glConfig.es_context (gl4es presents desktop GL 2.1 → false); the esStubs
  only fire for names gl4es fails to resolve (it resolves the full GL2
  surface, that is its job); gl2_shim additions are only reached through
  R_GetProcAddress's GLES3COMPAT branch; the gl_studio overflow guards are
  ref-agnostic safety fixes. The mobile quality preset v6 applies on gl4es
  (version 2.1 < 3.0): shadows/sunshafts/exposure/lensflare/refraction/
  portals/DoF/FSR off, renderscale 1.0, menu blur off, grass fades.

What this does NOT fix: the gl4es path was CPU-bound at 5-15 fps in the
v0.4.4 field log (80-160 ms scene times — per-call translation overhead).
That is the next round, and it must be data-driven: the perf heartbeat and
the low-fps watchdog are in the client and run under gl4es, so the next
engine.log tells us which stage eats the frame (scene vs postfx vs uploads,
dips/binds, polys per draw). No more blind patching.

## 2026-11-22 — v0.6.1: the 13:13 log decoded — renderer clean, three config fixes

The v0.6.0 field log (13:13-13:24, clean session end) rewrites the picture:

1. **Zero shader failures.** The "99+ renderer errors" are missing GAME
   DATA: ~65 sounds (hgrunt/barney/scientist/alice pain+attack, footsteps/
   grate, fvox, controller) + 2 models (bmec sedan_volkswagen_passat.mdl,
   bmec2 truck.mdl) + solid_sky/alpha_sky/lambda.lmp. The author's source
   repo ships no data files — these gaps ship with the game itself for
   everyone. Documented, not fabricated.

2. **The launch stall — my v0.5.0 precompile change.** "Precompiling
   shaders from source (no binary cache on this driver)" printed 369
   times: 714 permutations compiled at up to 2 s per frame (budget 3-50/
   frame), 0.5-1 fps behind the menu for 6+ minutes, EVERY launch (no
   binary cache on gl4es = nothing persists). The 13-76 s "lazy compile
   stalls" it was meant to prevent were actually the readback cliffs fixed
   in v0.2-v0.4. Fix: skip the pass without a binary cache — exactly the
   reference behavior (v0.4.4 had it right; v0.5.0 regressed it).

3. **LOD was OFF.** cl_lod_enable defaults 0 (desktop default, same in
   the reference). The menu world rendered 1,050,168 epoly (2,250,360 at
   worst), in-game 74,619 — every model at full detail at any distance.
   Watchdog verdicts said it outright: SCENE-BOUND, studio models, advice
   cl_lod_enable 1. Fix: force cl_lod_enable 1 in CStudioModelRenderer::
   Init under the mobile preset (overrides the archived 0 from v0.6.0
   sessions; models without embedded LOD levels unaffected; author
   default cl_lod_scale 5.0 untouched).

4. **Delivery: the libs zip was never needed.** The APK bundles the game
   libs (client/server/menu), all 74 shaders, particles and fonts; the
   log's "Adding directory: .../files/gamelibs/" confirms the game loads
   them from the APK, and APK assets are mounted as the highest-priority
   search path, shadowing anything extracted into the game folder. The
   "download 24 MB of libs every release" pain was my release
   instructions, not the tech. v0.6.1 ships APK-only; the release script
   verifies self-containment (renderers + game libs + glsl assets)
   before uploading.

Also noted for the next round: the 0.5 fps watchdog verdict "NOT
GPU-SCENE-BOUND (74.4 of 2041 ms)" during precompile — that bucket
mislabeled shader compilation as simulation; it disappears with the
precompile skip. Remaining watch item: dips 1162-2490 on the 1-2M epoly
menu frames (LOD should collapse these too).

## 2026-11-22 — v0.7.0: Performance Mode — the engine renderer takes the default slot

The v0.6.1 field data (13:58 log + two screenshots) closed the case on the
gl4es custom-renderer path:

- The 1-2 fps frames were NOT the 219k-epoly scenes — those ran 11-18 fps.
  The killers were my v0.6.1 precompile skip: it moved the compile storm
  INTO gameplay (31 s single frame at map load, 9 s mid-game). The 13:58
  log's spike at "Setting up renderer..." nails it.
- LOD was a no-op for the heavy scenes: StudioCheckLOD only finds LOD levels
  in models carrying a "studioLOD" bodypart; the cars/NPCs have none.
  219k-258k epoly is UNAVOIDABLE in the custom renderer path — it always
  draws full-detail models.
- The screenshots show the game rendering largely CORRECTLY through gl4es
  (menu, chapter select, cars, terrain, dusk sky) — the visible defects are
  specific magenta/cyan surfaces (texture-format failures on certain map
  assets) and the missing-data gaps. The renderer was never the problem;
  the architecture was.

Decision: performance mode as the shipped default. The client's custom
shader renderer is OFF (gl_custom_renderer 0, registered in V_Init, gate in
R_Init before any GL call). HUD_RenderFrame's existing contract does the
rest: return 0 when !g_fRenderInitialized → the ENGINE's ref renders the
world. This is field-proven on this exact device: v0.5.2 lived in this
state and the game played. ref_gles3compat is the default ref again
(native GLES, no per-call translation). ui_shadersdone=1 in the gate so
mainui never hides the menus. Engine-side: missing sounds report once per
session (the 1.21 data ships without ~65 referenced files); watchdog
advice no longer recommends cl_lod for models without studioLOD data.

Full-look mode stays: gl_custom_renderer 1 + -ref gl4es.

Research note (for the rewrite/panvk question): Diffusion = xash3d-fwgs
engine + a client-side GL 2.1 renderer with GLSL shaders; the author's own
Android port runs it through gl4es. There is no Vulkan path anywhere in
the project. panvk (Mesa's Mali Vulkan driver) targets Valhall but is
experimental; using it would mean writing a new renderer in Vulkan from
scratch against a moving driver — months of work with worse odds than the
paths we have already measured. The evidence-backed fast path ships today.

## 2026-11-22 — v0.7.1: the v0.7.0 crash, decoded in two parts

The 14:52 crash log + crash.log told a two-part story:

1. **SIGSEGV at 0x0 = NULL function pointer call.** The v0.7.0 gate
   returned from R_Init BEFORE GL_InitExtensions, so every client pgl
   pointer stayed NULL. The crosshair HUD draws with raw
   pglBindVertexArray/pglBufferSubDataARB/pglDrawArrays — first weapon
   switch after map load → call through NULL → crash right after the
   crosshair texture-load burst (the log's last lines). Fix: run
   R_InitRefState + GL_InitExtensions BEFORE the gate — the exact
   v0.5.2/v0.6.x-proven state where those pointers are resolved; the
   crosshair calls then hit real no-op GL entry points.

2. **gles3compat breaks the engine's texture path on this game's data.**
   Under v0.7.0 (gles3compat) EVERY .dds the HUD/world requested failed
   ("texture not found": health.dds, crosshairs, scopes, use icons) while
   the identical requests all succeeded under v0.6.1 (gl4es) — mounts
   identical, imagelib identical, only the ref differs. This is also the
   real explanation for v0.5.0's "black terrain" on the native path.
   Decision: performance mode runs on gl4es — the proven texture path —
   minus the client renderer (the measured cost driver). gles3compat
   stays -ref-selectable.

## 2026-11-22 — v0.7.2: the sky, the noise, and protecting the 90 fps

The v0.7.1 field report: 90 fps, log nearly clean. Three items left, all
load-time — the frame path is untouched:

1. THE SKY (the visible "gumbled/missing" candidate). Diffusion maps set
   no worldspawn skyname — the custom renderer drew a shader skydome and
   never needed one. The engine fell back to sv_skyname's default
   "desert", CheckSkybox("gfx/env/desert") found no files in the game
   data, and because the name equals DEFAULT_SKYBOX_NAME the miss warning
   is skipped (ref_common.c R_SetupSky) — so the sky silently never set
   up, and sky surfaces rendered with whatever was bound (garbage). Fix:
   a generated 6-face seam-free skybox (scripts/gen_skybox.py — color is
   a pure function of the sample direction; all 12 cube edges verify 0
   diff) shipped as gfx/env/desert{rt,bk,lf,ft,up,dn}.tga in the APK
   assets, which FS mounts at highest priority. Zero engine code change
   on the happy path; custom-named skies now also have a working
   fallback.

2. solid_sky/alpha_sky "texture not found" pairs: cleanup noise, not a
   failure. Mod_LoadTextures frees old sky layers via
   R_GetBuiltinTexture(name), which LOADS by name when absent — on maps
   without a Quake-style sky that meant two failed file searches per
   world load. Fixed with Find-then-free.

3. "Unknown command cl_lod_scale / cl_viewmodel_fov": the studio
   renderer's Init() registers those; performance mode skips it, but the
   client's Initialize() execs diffusion.cfg anyway. Registered both in
   Initialize() before the exec (idempotent with the later registration
   in full-look mode).

Performance guard: every change is texture generation, cleanup and cvar
registration — all load-time. The 90 fps frame path (engine renderer, no
client shaders) is byte-identical.
