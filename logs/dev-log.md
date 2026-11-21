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
