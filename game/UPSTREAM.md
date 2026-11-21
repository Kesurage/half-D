# Vendored: Diffusion game source (client/server/glsl)

- Base: https://github.com/yalnie/Diffusion-Android @ b63a4cf (master)
  - fork of Aynekko/Diffusion (upstream through Nov 17 2026) with the
    Android/GLES/ARM renderer fixes: tone-mapping exposure texture and
    luminance capture order, SMAA render-target formats, 76-bone skinning
    cap (Mali/Adreno driver limit), signed boneid + clamping, GLSL type
    strictness with #version 130 kept (gl4es translates), case-insensitive
    shader load, world visleaf guards, FBO mip clears, engine gles-branch
    support (GLES 3.0 context + GL2 shim GetProcAddress), BSP lump
    realignment. His working APK: github.com/yalnie/Diffusion-Android
    releases (continuous).
  - v0.3.0 note: our v0.2.4 merge had taken Aynekko master directly and
    dropped these fixes — that is what broke lighting/models on device.
- License: GPL-3.0 (client/render/* is Uncle Mike's Xash custom renderer).
- Local changes (half-D fork, re-applied on the yalnie base):
  1. scripts/waifulib/xcompile.py + host_tool_compiler.py (from the engine
     tree) + wscript loads xshlib/xcompile: Android cross-compile via
     `waf configure --android=aarch64,clang,21`; game_launch desktop-only.
  2. wscript: -std=gnu++14 pin ('register' in stringlib.cpp predates C++17).
  3. view.cpp: mobile quality preset (gl_mobile_quality 1) — shadows/
     sunshafts/exposure/lensflare/refraction/portals off, grass fades
     shortened, FSR off (per-frame pglCopyTexSubImage2D = gl4es CPU
     readback), renderscale 1.0 (DownScale does the same copy at < 1).
  4. r_main.cpp: perf heartbeat with per-stage ms (gl_diag 1).
  5. r_misc.cpp: terrain 2D single-layer fallback without texture arrays.
  6. r_postprocess.cpp: one-time warning when DownScale engages.
  7. 3rd-party/discord-rpc vendored as plain source (unused on Android).
- Content (maps, models, sounds) is NOT here; it comes from the itch.io
  v1.21 download. The glsl/ folder ships with the game folder.
