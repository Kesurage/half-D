# Vendored: Xash3D FWGS engine fork

- Upstream: https://github.com/FWGS/xash3d-fwgs
- Pinned commit: 11ed246c885d710ca1682370c66ee6594ac2d25a (master, 2026-11-20)
- mainui submodule pinned 8981066 (FWGS/mainui_cpp) — vendored separately after clone hiccup
- Vendored as plain source (no .git, no .github). Submodules vendored at their
  pinned commits: nanogl cf7ccf7, gl4es a025615b (ptitSeb/gl4es), vorbis bb4047d,
  opus 82ac57d, libogg 7cf42ea, opusfile 3ecc22a, mpg123 20c59b0, mbedtls 7238fc3
  (FWGS/mbedtls-releases), libbacktrace 0b9b49c, MultiEmulator e369abc,
  library_suffix 0d4aec0. bzip2 vendored from the libarchive mirror
  (github.com/libarchive/bzip2 @ 1ea1ac1) because gitlab.com is unreachable
  from the dev sandbox; only the plain sources are needed. gl-wes-v2/maintui/
  glslang/opengnm not vendored (unused by the Android build).
- Diffusion tracks FWGS master (Aynekko's CI pulls the continuous AppImage), so
  this pin is our baseline.

## Local changes (half-D fork)

Diffusion's client-side renderer requires a desktop GL compatibility context
(CONTEXT_TYPE_GL); only ref_gl4es provides one on Android. v0.1.2 taught the
launcher to pass `-ref gl4es`, but the launcher's Game.kt always sends a
non-null argv (built from saved prefs), so that default never fired and the
device still booted gles1. v0.1.3 forces gl4es at three layers:

1. `engine/client/dll_int/ref_common.c`: on XASH_ANDROID, after an explicit
   `-ref` argument (which still wins), try `gl4es` before the saved `r_refdll`
   cvar — so a stale video.cfg cannot regress to gles1. If gl4es fails to
   load, the normal cvar/autodetect flow continues (gles1 stays last resort).
2. `android/.../model/Game.kt`: launcher default arguments now
   `-console -log -ref gl4es` (covers fresh installs before any engine logic).
3. `android/.../XashActivity.java`: null-argv fallback default also
   `-console -log -ref gl4es` (covers external intents/shortcuts).
4. `android/.../XashActivity.java`: extracts the bundled menu font
   (assets/gfx/fonts/FiraSans-Regular.ttf, SIL OFL) into
   `<rodir>/<gamedir>/gfx/fonts/` on launch — MainUI loads
   `gfx/fonts/FiraSans-Regular.ttf` from the engine search paths and the itch
   download doesn't ship it.
