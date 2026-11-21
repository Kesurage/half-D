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

  8. client/render/r_studio.cpp (v0.1.6): StudioLoadTBN never loads
     cache/<model>.tbn from disk — the shipped desktop-generated caches have
     submodel vertex offsets that don't match this build, and the sanity
     assert in StudioCreateMeshCache (r_studio.cpp:1733) fatal'd the game
     during map load. TBN data is now always regenerated (deterministic,
     in-process). The vertex-light sanity checks at the same site are
     downgraded to non-fatal warnings, and the dml block got the missing
     j != MAXSTUDIOMODELS guard (the old ASSERT indexed submodels[j] out of
     bounds when the offset search failed).

  9. client/render/r_grass.cpp (v0.1.7): both R_DrawGrass passes dereferenced
     RI->currentshader right after GL_BindShader without a NULL check —
     when a grass/fog shader failed to compile (GL 2.x), the fog path
     segfaulted at u_FogParams (offset 0x524 of a NULL program pointer).
     Guarded; a missing shader now skips the uniform upload.

  10. glsl (v0.1.8): eight ES 1.00 violations fixed so their shaders compile
     on GLES2 contexts: int literals in float arithmetic (drawshafts
     `accum /= 28`, motionblur `Multiplier * N`, lensflare `circColor + N`,
     luminance `1 + lum`, heat `linearizeDepth( 4096, ...)`, grassdepth
     `<= 50`), `round()` (waterdrops, replaced with floor(x+0.5)), and the
     `%` operator (dronescreen, rewritten with mod/floor on floats).
     Glitch (uint bitops) and SMAA (textureLodOffset) remain GLSL 130-only.
  11. client/render/r_postprocess.cpp (v0.1.8): every post effect now starts
     with a CheckShader guard - a shader that failed to compile skips the
     effect instead of dereferencing a NULL program pointer (upstream
     RenderSunShafts already had this; MotionBlur, HorizontalBlur,
     ScreenWater, Glitch, Monochrome, WaterDrops, LensFlare,
     DroneScreenShader, SMAA and Bloom did not). Bloom and ToneMap are
     additionally gated on GL 3.0+: their screen-sized NPOT texture with a
     mip chain and mip-level FBO attachments cannot work on GLES2
     (glGenerateMipmap fails on NPOT, gl4es's software-mipmap upload into
     the unallocated levels crashed the Mali driver).

  12. glsl (v0.1.9): emboss.h and alpha2coverage.h get gl4es-aware stubs -
     textureSize(), array constructors and sampler2DArray do not survive
     the translation to ES 1.00 and took down every Bmodel*/Studio*
     shader family. Under GL4ES (defined by the gl4es translator) the
     emboss detail filter and the mip-based alpha rescale become no-ops;
     desktop GL keeps the originals. Terrain (BMODEL_MULTI_LAYERS) stays
     GL 3.0+-only: it needs texture arrays for the layer map.
  13. client/render/r_postprocess.cpp (v0.1.9): RenderSunShafts is gated
     on GL 3.0+. It copies the full screen (color AND depth) into
     textures every frame; under GLES2 the color copy is a full-screen
     CPU readback and the depth copy fails outright, so the effect was
     the main per-frame stall and visually broken at the same time.

  14. render/r_local.h (v0.2.0): TF_SCREEN gains TF_HAS_ALPHA. Screen
     textures used to pick GL_RGB8, and gl4es only forwards
     glCopyTexSubImage2D to the GLES driver for RGBA/UNSIGNED_BYTE
     textures - so every full-screen copy in the post chain (motion blur,
     screen water, monochrome, sharpen, sun shafts, SSAO...) was a 4 MB
     CPU readback per frame per effect (~4 fps on Mali-G57). RGBA makes
     them native GPU copies; the effects stay enabled.

  15. render (v0.2.1): diagnostics layer — a failed GLSL permutation is now
     cached as failed (upstream wiped it, so every runtime request re-ran a
     100-300 ms driver compile: the mid-game fps dips); every program
     compile is logged with options + duration; a 5-second performance
     heartbeat (fps, frame ms, scene polys) and >150 ms spike lines go to
     engine.log, gated by gl_diag (default on).
  16. glsl (v0.2.1): interior.h/bmodelsolid/bmodeldlight had 'const float
     x = 500;' / '= -4096;' — int literals. Strict GLSL ES 1.00 (the Mali
     GLES2 path through gl4es) has no implicit int->float conversion, so
     every BMODEL_INTERIOR / BMODEL_WATER_REFRACTION permutation failed
     with S0001 — the invisible terrain. Made them float literals.

  17. glsl (v0.2.2): interior.h called randomFunction2( 1, ... ) 26 times
     with an int literal for a float parameter ('S0001: No matching
     overload' on strict GLSL ES 1.00 — overload resolution has no
     implicit conversions either); mix( 0, ... ) twice in grassdlight/
     studiodecal. Float literals now.
  18. engine (v0.2.2): load-stage timing in SV_SpawnServer/SV_ActivateServer
     — world model, submodels, game-DLL activate (precache+spawn), settle
     frames each print a duration, to attribute the 32-36 s chapter loads
     seen in the v0.2.1 device log.

  19. glsl (v0.2.3): offline ES 1.00 validator sweep — all 384 permutations
     now compile under Mali-strict rules. interior.h (1 - diffuse.a) x5,
     InteriorMapping(..., 0, ...) float-param x4, grass mod(x,4)/<=50 int
     literals, glitch_fp uint/uvec hash replaced with float hash33,
     shadow samplers (sampler2DShadow/samplerCubeShadow/shadow2D/shadowCube
     are GLSL 1.30+) emulated under GL4ES via depth-texture + step(),
     BMODEL_MULTI_LAYERS sampler2DArray blocks gated on
     GLSL_ALLOW_TEXTURE_ARRAY (the type does not exist in the ES 1.00
     grammar — terrain fell back to single-texture on GL 2.1).
  20. render (v0.2.3): prefix no longer emits MAXSTUDIOBONES when the
     permutation defines its own (macro redefinition killed all 373
     studio permutations on Mali = invisible characters); round()/
     textureLod()/textureLodOffset() shims on the GL 2.x path;
     GLSL_ALLOW_TEXTURE_ARRAY only on GL 3.0+.
  21. render (v0.2.3): mobile quality preset (gl_mobile_quality, default 1
     on GL 2.x) — renderscale 0.85, shadows/sun-shadow/grass-shadows off,
     sunshafts/exposure/lensflare/water-refraction/portals/DoF off,
     mirror quality 1, grass fade 512-1024.

  22. (v0.2.4) MERGE from yalnie/Diffusion-Android (shared with permission,
     2026-11-21): his tree is the same architecture but took the opposite
     shader approach — #version 130 kept, gl4es translates to ES 3.00 on
     Mali-G57, with source-level #ifdef GL_ES blocks (precision, shadow-
     sampler emulation, texel-offset fallbacks). That keeps round(),
     textureLodOffset, shadow samplers and texture arrays alive and avoids
     the whole ES 1.00 strictness class. Merged: client/, server/, common/,
     game_shared/, glsl/ (~1600 commits of upstream Diffusion + his device
     fixes: ARM64 studio boneid sign/limits, world visleaf guard, FBO mip
     clear, SMAA texture formats + validation, ToneMap luminance capture,
     cubemap/subview/frustum/particles reworks, assert-crash guards,
     case-sensitivity and int/float mismatch fixes across all shaders).
     Kept ours: engine/ (launcher, gl4es forcing, APK asset layer), build
     glue, CI. Re-applied on top: MAXSTUDIOBONES macro-redefinition guard
     (r_shader.cpp), gl_mobile_quality preset (view.cpp).
