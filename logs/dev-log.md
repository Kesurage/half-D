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
