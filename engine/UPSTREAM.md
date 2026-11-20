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
