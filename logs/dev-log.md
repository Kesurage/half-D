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
