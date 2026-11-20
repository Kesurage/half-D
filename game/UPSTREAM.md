# Vendored: Diffusion game source (client/server/glsl)

- Upstream: https://github.com/Aynekko/Diffusion
- Pinned commit: caaa327c2cbde1a6839592a6d7224cdf23b13a90 (master)
- License: GPL-3.0 (client/render/* is Uncle Mike's Xash custom renderer, GPL-3.0).
- Local changes (half-D fork):
  1. scripts/waifulib/xcompile.py + xshlib.py copied from the engine (Android
     cross-compile support; Diffusion's waf lacked --android).
  2. wscript: loads xshlib/xcompile; subprojects conditional — on Android builds
     client + server + mainui_cpp (game_launch is desktop-only).
- Content (maps, models, sounds) is NOT here; it comes from the itch.io v1.21
  download. The glsl/ folder IS vendored (shaders ship with the game folder).
- discord-rpc submodule: present but unused on Android (desktop only).
