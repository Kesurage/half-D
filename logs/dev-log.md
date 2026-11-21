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
