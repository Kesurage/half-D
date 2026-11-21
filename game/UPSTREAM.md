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
  3. server/physics/physx/physx_impl.cpp: FreePhysic() guard — if PhysX was
     never initialized (m_pFoundation == nullptr, e.g. client aborts with
     Host_ErrorInit before a map loads) skip PxCloseExtensions(); calling it
     without a foundation crashed on device (signal 11 in PxCloseExtensions).

  4. client/render/r_shader.cpp (v0.1.4, GL 2.x / gl4es compatibility):
     GL_ProcessShader hardcoded `#version 130` on every shader; on GL 2.1
     contexts (gl4es on Android) nothing compiles and the first shader init
     assert-fatal'd the game. Now the version is picked from glConfig.version
     (130 only on GL 3.0+, else 120/110) and `#define texture texture2D` is
     injected below GL 3.0 (dronescreen/waterdrops use the 130-era generic
     texture() call). Also: shader compile/link failures and missing shader
     files now log unconditionally (ALERT is silent without -dev, which hid
     every failure in engine.log), the 34 init-time uniform inits are guarded
     so a failed optional effect shader (SMAA, FSR, glitch, emboss...) skips
     instead of crashing (runtime paths already guard via CheckShader), and
     the Android launcher passes -dev 2 so device logs carry diagnostics.
     Known residual: smaa.h/emboss.h/glitch_fp/alpha2coverage.h use
     textureLod/textureSize (GLSL 1.30+) — those effects stay broken on
     GL 2.x until a compat pass; the core world shaders are pure GLSL 1.10.

  5. client/render/r_world.cpp Mod_LinkLeafLights (v0.1.5): once all leaflights
     are consumed (j == numleaflights) the loop kept dereferencing lp one past
     the array end for every remaining leaf — harmless OOB read on desktop
     heap, SIGSEGV on Android's hardened allocator (guard page). Now guarded
     with `j < world->numleaflights` instead of `world->numleaflights > 0`.
  6. glsl/* (v0.1.5, GLSL ES 1.00 compatibility via gl4es): swept the
     desktop-only `const in` parameter qualifier to plain `in` (12 sites,
     mathlib.h/matrix.h/shadow_omni.h/studiosolid_vp/terrain.h/tnbasis.h) —
     GLSL ES 1.00 allows only a single parameter qualifier, and under gl4es
     every shader including these headers failed to compile while every
     shader without them passed. Also made mathlib.h's implicit int->float
     conversions explicit (float(sampleIndex)*..., += 360.0, < 0.0) — ES 1.00
     has no implicit conversions.
  7. client/render/r_shader.cpp (v0.1.5): shader compile/link failures now
     print the driver info log unconditionally via Con_Printf — ALERT()/Msg()
     are gated by developer level and v0.1.4's engine.log showed 'shader X
     failed to compile/link' with no error text at all.
