#!/usr/bin/env python3
"""Rewrite all half-D GitHub release titles/bodies in plain changelog style."""
import json
import urllib.request

TOKEN = open('/home/z/my-project/.cred/token').read().strip()
API = 'https://api.github.com/repos/Kesurage/half-D/releases'

def hdrs():
    return {
        'Authorization': f'token {TOKEN}',
        'Accept': 'application/vnd.github+json',
        'Content-Type': 'application/json',
    }

R = {}  # tag -> (name, body)

R['v0.1.0'] = ('v0.1.0 — first build: engine APK + Diffusion arm64 libraries', '''First build of the port.

- Engine APK (arm64-v8a + armeabi-v7a) built from the vendored xash3d-fwgs fork
- Diffusion game libraries cross-compiled for arm64: client, server (PhysX), menu + glsl shaders
- All sources pinned at upstream commits (see the UPSTREAM.md files)

Known issues: library file naming did not match the engine's Android lookup — fixed in v0.1.1.''')

R['v0.1.1'] = ('v0.1.1 — fix: game libs bundled in APK (device-test round 1)', '''- Renamed library outputs to the engine's Android naming (libclient_android_arm64.so etc.)
- All three game libs now baked into the APK (jniLibs/arm64-v8a) — no separate download
- Library lookup chain documented in docs/PORTING_NOTES.md §7

Device test: engine boots, loads the game folder, initializes video — then Host_ErrorInit
because the game requires the OpenGL renderer. Renderer selection is next.''')

R['v0.1.2'] = ('v0.1.2 — renderer: gl4es default, PhysX shutdown guard, menu font', '''- Launcher default argv now selects -ref gl4es
- PhysX close-extensions guarded against uninitialized physics during error shutdown
- FiraSans-Regular.ttf (SIL OFL) bundled for the menu UI

Device test: launcher argv handling turned out to always override the activity default —
fixed at engine level in v0.1.3.''')

R['v0.1.3'] = ('v0.1.3 — gl4es forced at engine level, menu font auto-installed', '''- gl4es is forced in ref_common.c on Android (after explicit -ref, before saved r_refdll)
- Game.kt / XashActivity defaults fixed so the renderer choice always reaches the engine
- Menu font extracted from APK assets into the game dir at launch

Device test: gl4es context comes up, then the client's shader compile asserts — the shaders
hardcode #version 130 and gl4es presents GL 2.1. Fixed in v0.1.4.''')

R['v0.1.4'] = ('v0.1.4 — GLSL version fix: shaders compile on the gl4es GL 2.1 context', '''- GLSL version picked from glConfig.version (130/120/110) with a texture() -> texture2D define below GL 3.0
- Shader compile/link failures always logged (ALERT was developer-gated and silent)
- -dev 2 in launcher defaults so diagnostics reach engine.log
- 34 optional-effect shader init sites guarded so a failed effect skips instead of aborting

Device test: boots to menu, starts map load, SIGSEGV in Mod_LinkLeafLights — off-by-one
past the end of the leaflights array. Fixed in v0.1.5 along with the ES 1.00 mathlib issues.''')

R['v0.1.5'] = ('v0.1.5 — map-load crash fixed; shaders now speak GLSL ES 1.00 (gl4es)', '''- Guarded the one-past-end leaflights read in Mod_LinkLeafLights
- const in parameter qualifiers removed from 12 sites across 6 shader headers (invalid in ES 1.00)
- Implicit int->float casts fixed in mathlib.h
- Driver info logs now always land in engine.log

Device test: map load reaches studio mesh cache and asserts — the itch package ships
desktop-generated .tbn cache files whose offsets don't match this build. v0.1.6.''')

R['v0.1.6'] = ('v0.1.6 — studio caches: stop trusting desktop-generated .tbn files', '''- StudioLoadTBN always returns false — TBN caches are regenerated in-process
- TBN/dml sanity checks downgraded to warnings
- Bounds guard before dml->submodels[j]

Device test: the render loop is live and crashes in R_DrawGrass (NULL shader ->
NULL uniform base). The engine.log also shows the device compiled the original itch
glsl from the game folder, shadowing all shipped fixes. v0.1.7.''')

R['v0.1.7'] = ('v0.1.7 — patched glsl ships in the APK; world renders', '''- APK assets are mounted as the highest-priority search path (FS_Rescan override)
- Patched glsl packed into APK assets by CI
- Grass renderer guards both currentshader dereferences

Device test: world renders; crash moves into the bloom pass — glCopyTexSubImage2D
readback into an NPOT texture with a mip chain faults the driver. v0.1.8.''')

R['v0.1.8'] = ('v0.1.8 — post-effects over GLES2: 8 more shader fixes, bloom gated', '''- Bloom/ToneMap gated on GL 3.x (their copy paths cannot survive GLES2)
- 8 ES 1.00 shader violations fixed
- CheckShader guards on all post effects (MotionBlur ran unconditionally)

Device test: game loads and plays; case-sensitive APK asset lookup missed 10 shaders
and the sun-shaft pass still does per-frame readbacks. v0.1.9.''')

R['v0.1.9'] = ('v0.1.9 — case-insensitive asset lookup, ES-safe emboss/luminance, touch art', '''- Case-insensitive retry in FS_FindFile_AndroidAssets (AAssetManager is case-sensitive; mixed-case shader names missed the override)
- emboss.h/alpha2coverage.h become no-op stubs under GLES2 (need textureSize/GLSL 120)
- Sun shafts gated on GL 3.x (fullscreen color+depth copies per frame)
- Touch button pack generated and shipped in APK assets

First build with working touch controls end to end.''')

R['v0.2.0'] = ('v0.2.0 — the FPS fix: RGBA screen textures (GPU copies instead of readbacks)', '''- Screen textures are now GL_RGBA8 — gl4es only passes glCopyTexSubImage2D to the driver for RGBA/UBYTE, so every post effect did a ~4 MB CPU readback per frame before; now they are native GPU copies
- The case-insensitive fallback returned the original mixed-case name and failed to open — now returns the corrected name

Measured: 10 fps (up from 4), trees/cars/characters visible, cutscenes render.''')

R['v0.2.1'] = ('v0.2.1 — ES 1.00 int/float fixes, negative shader cache, diagnostics layer', '''- Bmodel shaders: int literals for float constants (staticSeed, u_zFar) fixed — ES 1.00 has no implicit conversions
- Failed shader programs are now cached as failed — no more re-compiling dead permutations every frame
- Diagnostics layer: shader compile trace, 5 s perf heartbeat, >150 ms spike lines, gl_diag cvar

Measured: epoly-bound at 12-17 fps (80-100k studio polys), 49 fps idle, two ~35 s chapter-load spikes.''')

R['v0.2.2'] = ('v0.2.2 — 26 more float-literal fixes, per-stage load timing', '''- Interior shader family: randomFunction2( 1, ... ) x26 — int literal for a float parameter in every permutation; fixed plus two mix(0,) sites
- Engine load-stage timing in SV_SpawnServer/SV_ActivateServer to attribute the ~36 s chapter loads

Heartbeat now distinguishes epoly-bound frames from dips. Ground textures render again (BmodelSolid family compiles).''')

R['v0.2.3'] = ('v0.2.3 — 384 permutations, 0 failures (offline GLSL validator)', '''- scripts/validate_es100.py: replicates the ref's preprocessing and the gl4es translation, runs every shaderlist permutation through glslangValidator
- Every remaining ES 1.00 violation fixed in one pass instead of one per device round
- Mobile quality preset (gl_mobile_quality) on the GL 2.x path

Offline validation: 384/384 permutations clean.''')

R['v0.2.4'] = ('v0.2.4 — merged the Diffusion-Android game tree (#version 130 path)', '''- Merged the Diffusion-Android game tree (558 files: client/server/common/game_shared/glsl) into game/
- Kept our engine/build/CI; re-applied the MAXSTUDIOBONES guard and mobile quality preset on top
- The #version 130 + gl4es->ES 3.00 translation strategy replaces our ES 1.00 rewrite — keeps round(), textureLodOffset, shadow samplers and texture arrays alive on Mali

Validation: 384/384 permutations clean under desktop GLSL 130.''')

R['v0.2.5'] = ('v0.2.5 — heartbeat, grass pass off, FSR1 on, audio underrun fix', '''- _snd_mixahead Android default 0.3 (per-frame underruns under 140 ms frames)
- Preset v2: r_grass 0, r_grass_lighting 0 (per-frame grass pass the preset never covered), gl_fsr 1 for the 0.85 upscale
- Heartbeat restored after the merge; GL caps line at ref init

Device test: graphics stable at 7 fps; audio fixed. FSR/DownScale readbacks are next.''')

R['v0.2.6'] = ('v0.2.6 — stable build: FSR readback cliff fixed, purple terrain fixed', '''- FSR() ran at any renderscale with a per-frame pglCopyTexSubImage2D — the same gl4es CPU-readback cliff; preset now gl_fsr 0
- LoadTerrainLayers requested texture arrays the ref cannot serve -> GL_NONE stub -> purple checkers; single-layer 2D fallback added
- Kept: heartbeat, GL caps line, _snd_mixahead 0.3

First stable build — correct visuals, playable frame times.''')

R['v0.2.7'] = ('v0.2.7 — DownScale was the second readback; per-stage heartbeat, ref texture-miss logging', '''- DownScale() at renderscale<1 does the same per-frame copy as FSR — preset v4 pins renderscale 1.0
- Heartbeat now reports per-stage ms (scene/postfx/anim/cube)
- Ref logs texture misses and upload failures by name; caps2 line for s3tc/etc1/astc/fbo

Scene is now the dominant cost; the readback cliffs are all gated.''')

R['v0.3.0'] = ('v0.3.0 — rebased the game tree onto Diffusion-Android (the proven renderer base)', '''Research round: no engine fork exists upstream — the game is a stock fwgs engine plus a client-side renderer, and Diffusion-Android is the working Android port of it.

- game/ vendored from Diffusion-Android b63a4cf (exposure/tone-mapping fixes, SMAA formats, 76-bone Mali cap, signed boneid, GLSL strictness, BSP alignment)
- Our patches re-applied: preset v5, heartbeat + stage timing, terrain fallback, DownScale warning, waf --android flow
- Engine: +4 upstream commits (GLES3 ctx/GL2 shim, levelshot guard, BSP realign); --enable-gles3compat added

The ES 1.00 rewrite is dropped in favor of the #version 130 + gl4es translation path.''')

R['v0.4.0'] = ('v0.4.0 — menu crash fixed, dynamic lights restored, shader disk cache on gl4es', '''Four independent fixes from the first v0.3.0 device log:

1. Menu blur ran an ungated double fullscreen readback while any menu was open -> multi-second frames -> GPU watchdog. Now behind r_menu_blur (off in the mobile preset).
2. Dynamic lights were dead: no GL_ARB_depth_texture via gl4es -> TF_SHADOW stubs return 0 -> every dlight pass aborted. Shadowless white stubs now serve instead.
3. Shader disk cache: the extension table required glProgramParameteri, which gl4es does not export -> R_BINARY_SHADER_EXT never enabled. Table now needs only the get/set pair; the hint setter is resolved separately and NULL-guarded. Precompile + .shadercache persistence work.
4. Console font maps to the bundled FiraSans (SIL OFL).

Open: menu background map renders ~282k epoly; raindrop.dds upload failure (BC7 without BPTC).''')

R['v0.4.1'] = ('v0.4.1 — options-menu crash rooted out: hard gate on all screen-capture effects', '''Root cause of the options crash: the video presets re-armed per-frame fullscreen readbacks (bloom, sunshafts, blur, hbao, refraction...) that the mobile preset had turned off — the same readback cliff, triggered by touching any graphics setting.

- R_AllowScreenCaptureFX hard gate at the top of all 11 capture functions — they stay off on the GL2 path regardless of cvars (gl_mobile_quality 0 opts back in)
- Shader failures logged with name/options/stage (279 were silent — ALERT was developer-gated)
- Negative cache: failed permutations are never retried this session; precompile filter skips ~413 device-impossible entries (shadow samplers + texture arrays)
- Load timing extended: studio-cache purge, FS reads, lump swap, FindBSPX
- raindrop.dds (BC7 SRGB, no BPTC on Mali) replaced with an uncompressed BGRA procedural streak''')

R['v0.4.2'] = ('v0.4.2 — heap corruption from the v0.4.1 raindrop.dds fixed', '''The v0.4.1 crashes were a corrupted heap, not game code: the generated raindrop.dds set the DDSD_PITCH flag with a per-row pitch value, and the loader's PITCH|LINEARSIZE branch used it as the TOTAL size — a 128-byte allocation for a 16 KB texture, then 16 KB written through it.

- Generator rewritten: DDSD_LINEARSIZE with the full W*H*4
- Loader hardened: a PITCH-flagged uncompressed file now computes pitch * height instead of trusting the field — no tool-generated DDS can misallocate the buffer again

All v0.4.1 fixes (capture gate, negative cache, precompile filter, load timing) ride along.''')

R['v0.4.3'] = ('v0.4.3 — engine restart lifecycle crash fixed (stale FS pool, survivable errors)', '''Crash chain: the engine was restarted inside the Android process; Memory_Init resets the pool table, but the filesystem library kept a pool handle from the previous lifetime; Mem_FindPool Sys_Errors on it — and that error was survivable-but-lethal (shutdown_issued never reset after a restart), so execution continued past a fatal error into a NULL dereference.

- Mem_FindPool degrades loudly instead of Sys_Erroring
- FS_InitMemory probes the inherited pool handle and takes a fresh pool when stale (self-heal across restarts)
- All searchpath allocations checked; a failed mount skips with a named error instead of crashing
- host.shutdown_issued reset at init; engine.log appends across in-process restarts so the original first error survives''')

R['v0.4.4'] = ('v0.4.4 — shader binary cache reverted (menu-world crash + visual glitches), gl4es guarded', '''Crash in gl4es fpe_CustomShader on source-less programs: the v0.4.0 change enabled the program-binary cache, but a binary-loaded program has no gl4es-tracked shader source, and the first fixed-function state change (fog/alpha in the menu world) dereferenced NULL. The same source-less programs explain the visual glitches — fixed-function state never got baked into them.

- binaryshaderfuncs requires glProgramParameteri again — the disk cache is off under gl4es, all programs compile from source (reference behavior); the pointer-table copy-paste bug stays fixed
- gl4es fpe.c: fpe_CustomShader guards against source-less programs and allocation failure (upstream lacks this)
- assets/particles: 4 placeholder DDS for textures referenced by particles.txt but absent from the 1.21 data — debris renders instead of checkers

Forensics: vendored gl4es is content-identical to the reference pin; the delta was the local patch stack.''')

R['v0.5.0'] = ('v0.5.0 — native GLES 3 renderer port (no gl4es translation on the draw path)', '''Experimental: run the client's GL2 renderer natively on the GLES 3.2 context instead of through gl4es per-call translation.

Client: ES context detection, on-the-fly GLSL ES 300 transform (#version 300 es, texture2D family -> texture, gl_FragColor -> out, varying/attribute -> in/out, FF matrices -> uniforms, FF vertex attribs -> layout-pinned), ARB object API wrappers, generic suffix retry.
Engine: gl2_shim matrix stack + immediate-mode variants + exports, GL_QUADS array expansion.
Precompile decoupled from the binary cache and runs behind the loading screen.

Offline validation: tools/validate_es300.py — 1141/1141 permutations clean as GLSL ES 300 (found and fixed samplerCubeShadow precision, shadow2D mapping, the reserved word 'sample', texture2DProj, gl_ClipVertex, gl_Vertex).

Known limitations: studio decals through the shim FF program; shadow permutations untested on device; gl4es stays selectable.''')

R['v0.5.1'] = ('v0.5.1 — crash fix: extension checks now honor core capabilities on ES contexts', '''First native-GLES3 run: the port worked end to end (renderer loaded, menu world up, map load started), then a NULL worldmodel deref in the weapon-obstacle trace.

Root cause: GL_CheckExtension required desktop GL_ARB_*/GL_EXT_* extension strings before trying the function table — native GLES3 has multitexture in core and never lists the string, so the custom renderer disabled itself one line after the context came up.

- On ES contexts the string precheck is skipped; the function table is the source of truth
- Defensive guard at the crash site: obstacle trace only runs with a live worldmodel''')

R['v0.5.2'] = ('v0.5.2 — capability cvar veto fixed, mesh batch overflow guards', '''Round two of the multitexture veto: with cvar_from_engine the client read the ENGINE's gl_arb_multitexture cvar, which the native ES ref never sets — the check failed before reaching the function table.

- On ES contexts the capability-cvar value is ignored; the function table decides (r_extensions master switch still honored)
- GL_MAX_TEXTURE_UNITS fails on native ES3; falls back to GL_MAX_TEXTURE_IMAGE_UNITS like the engine ref
- Engine ref: all three studio array mesh builders truncate batches that would overflow the fixed vertex/element arrays instead of writing past them (high-poly models)''')

R['v0.5.3'] = ('v0.5.3 — function-table audit complete (277/277), low-fps watchdog added', '''- tools/audit_v053.py models the client's GL_CheckExtension resolution chain for every name in every dllfunc_t table: found the four gaps (glDetachObjectARB, glMultiDrawArrays with GL_POLYGON, glGetQueryObjectivARB, glTexEnvfv) — all stubbed (DetachShader, per-batch TRIANGLE_FAN draws, uiv cast, NoOp). 277/277 names resolve on the native path.
- Verified the remaining init gates offline (shading_language_100, fragment_shader, FBO, program binary, hardware skinning >= 32 bones on Mali).
- Low-fps watchdog in R_DiagFrame: under 30 fps average it prints per-stage ms, dips/binds/polys and a verdict naming the dominant cost with the cvars to turn down — all cvar names verified to exist.''')

R['v0.5.4'] = ('v0.5.4 — shader prelude: no desktop-only #extension on ES (first full native init)', '''The client renderer initialized on the native path for the first time — then the shader preprocessor tripped on the prelude's own "#extension GL_EXT_gpu_shader4 : require": desktop-only directive, emitted after non-preprocessor tokens (Mali enforces P0001, desktop drivers tolerate it).

- The gpu_shader4 DIRECTIVE is not emitted on ES contexts (feature defines stay — ES3 has the ops in core)
- Body #extension lines from mod shaders are commented out on ES
- validate_es300.py now models the real native device profile (all 1141 permutations with gpu_shader4/texture_array ON, shadow + terrain included, plus a static no-extension-after-tokens check) — 1141/1141 clean''')

R['v0.6.0'] = ('v0.6.0 — back to gl4es: the native GLES3 bet didn\u2019t pay off', '''The v0.5.4 field run settled it: the custom renderer runs on the native path but renders wrong (black ground, error models, mesh/lighting garbage), at 0-4 fps, with device-reset crashes in the options menus. Regression against v0.4.4 on gl4es (correct visuals, working menus, 5-15 fps).

Forensics: the reference client renderer carries zero GLES-specific code — it expects a desktop GL 2.x context through gl4es. The text-level shader transform + call shim cannot reproduce it faithfully; every visual subsystem would be its own device-debug round trip.

- Renderer order swapped back: gl4es first, gles3compat the automatic fallback (-ref overrides); both refs still ship
- Verified inert under gl4es: every ES gate keys on es_context (false there), the esStubs only fire on names gl4es fails to resolve, the shim is only reached through the GLES3COMPAT branch
- Mobile quality preset v6 applies on gl4es (shadows/sunshafts/exposure/refraction/FSR off, renderscale 1.0, menu blur off)

Remaining known cost: the gl4es path is CPU-bound at 5-15 fps (per-call translation) — the next round is data-driven via the heartbeat and watchdog.''')

R['v0.6.1'] = ('v0.6.1 — precompile skip without binary cache, LOD on, APK-only delivery', '''The v0.6.0 device log decoded:

1. The "99+ renderer errors" are missing game data (~65 sounds, 2 vehicle models, sky lmps) — the author's source ships no data files; documented, not fabricated.
2. The launch stall was the v0.5.0 precompile change: 369 "Precompiling shaders" lines, 714 permutations at up to 2 s per frame, every launch, nothing persists without a binary cache. Fix: skip the pass without a binary cache — reference behavior.
3. cl_lod_enable defaulted 0: the menu world rendered 1-2M epoly (in-game 74k) at full detail. Now forced on under the mobile preset (models without embedded LOD levels are unaffected).
4. The libs zip was never needed — the APK is self-contained; the release script now verifies that (renderers + game libs + glsl assets) before uploading.''')

R['v0.7.0'] = ('v0.7.0 — Performance Mode: the engine renderer takes the default slot', '''Field data closed the case on the gl4es custom-renderer path: the 1-2 fps frames were the compile storm (31 s single frame at map load, 9 s mid-game), not the 219k-epoly scenes (those ran 11-18 fps); LOD was a no-op for models without studioLOD bodyparts; and the screenshots showed the game rendering largely correctly through gl4es.

- Performance mode as shipped default: the client's custom renderer is OFF (gl_custom_renderer 0, gate in R_Init before any GL call) — HUD_RenderFrame returns 0 and the ENGINE ref renders the world (the v0.5.2-proven state on this device)
- ref_gles3compat is the default ref (native GLES, no per-call translation)
- Engine: missing sounds report once per session; watchdog advice no longer recommends cl_lod for models without LOD data
- Full-look mode stays: gl_custom_renderer 1 + -ref gl4es

Research note: no Vulkan path exists anywhere in the project; panvk targets Valhall but is experimental — a from-scratch Vulkan renderer against a moving driver is worse odds than the measured paths.''')

R['v0.7.1'] = ('v0.7.1 — crash fix: resolve GL pointers before the renderer gate', '''Two-part crash story from the field log:

1. SIGSEGV at 0x0: the v0.7.0 gate returned from R_Init before GL_InitExtensions, leaving every client pgl pointer NULL — the crosshair HUD draws through them. Fix: R_InitRefState + GL_InitExtensions run before the gate (the pointers resolve to real no-op entry points in engine-renderer mode).
2. gles3compat breaks the engine's texture path on this game's data: every .dds the HUD/world requested failed ("texture not found") while identical requests succeeded under gl4es — also the real explanation for v0.5.0's black terrain on the native path. Decision: performance mode runs on gl4es (proven texture path) minus the client renderer; gles3compat stays -ref-selectable.''')

R['v0.7.2'] = ('v0.7.2 — generated skybox, load-time cleanups, 90 fps untouched', '''Field report: 90 fps, log nearly clean. Three load-time items:

1. The sky never set up: Diffusion maps set no worldspawn skyname; the engine fell back to "desert", found no files, and the miss warning is skipped for the default name — sky surfaces rendered garbage. Fix: a generated 6-face seam-free skybox (scripts/gen_skybox.py — color is a pure function of sample direction; all 12 cube edges verify 0 diff) shipped as gfx/env/desert*.tga in APK assets. Custom-named skies get the same fallback.
2. solid_sky/alpha_sky "not found" pairs: Mod_LoadTextures freed old sky layers by name-loading them first — find-then-free now.
3. cl_lod_scale/cl_viewmodel_fov registered in Initialize() so the diffusion.cfg exec stops warning in performance mode.

Every change is load-time; the 90 fps frame path is byte-identical.''')

R['v0.7.3'] = ('v0.7.3 — the full renderer is back: materials, lighting and effects are the product', '''The v0.7.2 field report closed the loop on the performance-mode trade: 90 fps, but "shader's loading" stuck on screen forever, cars oversized with missing components, no lighting, no effects.

1. The stuck bar: CHud::DrawShaderCompilation ran unconditionally, but its completion flag was only set at the end of the custom renderer's frame path — performance mode never reaches it. Now skipped when the renderer is off.
2. The broken world is what "no custom renderer" means: Diffusion's props, lighting and effects are client-renderer features the vanilla engine ref doesn't have. The v0.6.1 field data already proved the custom renderer draws this content correctly at 11-18 fps in the heaviest scenes.

Decision: gl_diffusion_renderer 1 (renamed so stale archived values cannot override the new default) is the shipped default; the 90 fps engine-renderer mode stays one cvar away. First map load stalls once while shaders compile lazily; the next round targets the 219k epoly floor.''')

R['v0.7.4'] = ('v0.7.4 — launch fixed: the compile storm moves behind the loading screen', '''The v0.7.3 report ("entire build flawed at 1fps") decoded:

- The stuck "Loading shaders" bar: its completion flag lived at the end of a frame path that wasn't compiling anything (no binary cache on gl4es — the per-frame pass returns immediately).
- The 1 fps: with the renderer on and no binary cache, every permutation compiles from source at first draw — 31 s frozen frame at map load, 9 s hitches mid-game.
- Confounder eliminated: ref_dll is FCVAR_RENDERINFO and persists in opengl.cfg — a device that saved "gles3compat" during v0.7.0 would keep the v0.5.0 configuration (garbage at 0-4 fps).

Fixes: R_PrecompileShadersSync(45 s) — budgeted synchronous compile of the full permutation list at map load behind the loading plaque; leftovers lazy. All done-flags settled inside every early exit. -ref gl4es forced on the launcher default command line. One startup banner line states ref / es_context / binary cache / compile mode for every future field log.''')

R['v0.8.0'] = ('v0.8.0 — freeze-crash fixed (budgeted shader compilation), heavy scene 4 passes to 1', '''The v0.7.4 device log decoded:

1. The crash: the 45 s sync pass compiled only 98 permutations (~460 ms each through gl4es); ~950 stayed lazy, and lazy compiles fire inside a frame — a new area queues 50-70 in one draw pass. The measured 32.7 s / 35.3 s frozen frames end in an Android watchdog kill.
2. The slideshow: the heavy outdoor scene ran FOUR view passes (437k epoly over ~110k unique) plus 33-41k grass at 0.3-4.4 fps — every SURF_REFLECT face re-renders the visible scene; r_mirrorquality was 1.
3. Binary cache stays off for the right reason: fpe_CustomShader cannot customize a binary-loaded program — cached programs would draw without fixed-function state.

Fixes: budgeted lazy compilation (r_shader_frame_ms, 60 ms/frame — over-budget draws fall back to a compiled family program and defer the real permutation; queue drains at frame starts and map load). Mobile preset v7: r_mirrorquality 0, grass fade 384/768. Heartbeat v2: pass breakdown + deferred-shader counters.''')

R['v0.8.1'] = ('v0.8.1 — v0.8.0 regression fixed: no wrong-shader drawing, no per-frame compile stalls', '''Both v0.8.0 regressions were in the new budget system, and the code explained each:

1. Blank textures: the family fallback drew surfaces with a different permutation of the same shader family — wrong texture/attribute contract, untextured output. There is no safe wrong permutation; removed entirely.
2. Low fps: the frame-start drain checked its deadline BETWEEN compiles — one compile is ~460 ms and always overruns a 60 ms budget, so every frame with a pending queue entry paid a full one.

Replacement: a rate limiter (r_shader_lazy_hz, default 1/s). A lazy miss either compiles inline (spending one token) or returns NULL — callers already handle NULL by skipping that surface for the frame and retrying (SURF_NODRAW / STUDIO_NF_NODRAW, cleared on success). Unseen materials pop in; nothing renders wrong; the deferred queue flushes unlimited at map load. 0 restores compile-on-first-use.

Also: CPU DXT1/3/5 decode in engine imagelib when the ref reports no hardware S3TC — the native-ES3 texture blocker (compressed upload is compiled out under XASH_GLES and the loader rejected every DDS). Verified by tools/test_dxt_decoder.c against hand-computed reference blocks. Dormant on gl4es.''')

R['v0.8.2'] = ('v0.8.2 — watchdog window closed, wrong-shader draws fixed, terrain fallback, session log archive', '''From the v0.8.1 field log:

1. Map-load shader drain is wall-clock capped (2 s) — the unlimited drain produced a 4489 ms frozen frame, one under the 5 s Android kill threshold.
2. Failed permutations are honored as failed: GL_FindUberShader returned dead negative-cache entries as valid and GL_BindShader kept the previous program — surfaces drew with a stale wrong shader. They now return NULL (surface skipped, not drawn wrong).
3. Multi-layer terrain falls back to single-layer on gl4es (texture arrays don't exist there) — landscape renders instead of never compiling.
4. Grass off in menu background maps; in-game fade 256/512.
5. Watchdog names compile stalls before scene-bound guesses.

Launcher: auto-applied device config (cfg/userconfig.cfg extracted per version bump), session log archive (last 10 sessions), logcat in crash bundles. Startup banner states the build.''')

R['v0.8.3'] = ('v0.8.3 — shader-starvation regression closed: per-frame load phase with progress bar', '''The 0.8.x trade of rendering correctness against frozen frames was itself the regression: deferring most shaders to a 1-2/s rate limiter meant black textures and invisible models until the queue drained, plus a ~460 ms hitch per compile.

- The whole shader list now compiles in a per-frame load phase (~1.2 s of compile work per frame, live "Loading shaders" progress bar, ANR-safe, once per session) — same total ~45 s, and when the bar settles, rendering is complete
- After the phase, shaders compile on first use like the reference port — always correct
- r_shader_lazy_hz defaults to 0 (rate limiter is opt-in)
- Grass off in the mobile preset (measured at 10x world poly count — the single biggest scene cost)
- The 42 data errors in the log are missing from the game data itself, not port bugs

First map after app start pays the one-time phase; subsequent maps are instant.''')

R['v0.8.4'] = ('v0.8.4 — default renderer flipped to the engine path (gl_diffusion_renderer 0)', '''Fix for the v0.8.3 regression (purple lighting, glitched characters/props, 5-8 fps):

- The field log reported 54 of 100 custom shader permutations skipped as unusable on this driver (no texture arrays, no gpu_shader4, no shadow samplers under gl4es)
- The permutations that do compile render wrong — the gl4es GLSL translation of the Diffusion lighting shaders produces the purple tint and glitched models; not a tuning problem
- The custom renderer drew correctly in v0.6.1, so this is a v0.6.1->v0.8.3 shader-path regression that needs on-device iteration CI can't do

gl_diffusion_renderer defaults to 0 again — the v0.7.0-proven engine-renderer path on this device (no shader compiles, upstream-tested batching, correct output at full speed). The auto-applied config corrects existing installs. The custom renderer stays opt-in for GPUs that expose the needed caps; fixing its regression is a separate on-device shader-debugging effort.''')

R['v0.8.5'] = ('v0.8.5 — custom renderer back ON + native ES3 ref default (gles3compat)', '''The engine renderer raised fps by drawing the game without its renderer — no game textures, dead dynamic lights, mis-scaled cars. The Diffusion materials and lighting live in the custom renderer; v0.8.5 restores it as default and moves it to the native GLES3 path:

- Default ref: gl4es -> gles3compat. The purple lighting and glitched models were the gl4es GLSL->GLSL-ES translation of the game's shaders; the long compile was ~460 ms per translated compile with no binary cache. The game already contains the complete GLSL ES 300 transform (v0.5.x groundwork) — now it's used. The v0.5.0-era native failures are fixed at source (DDS rejection removed by the v0.8.1 CPU DXT decoder; the compile storm rebuilt as the budgeted post-load phase).
- The v0.8.4 config had pinned r_shader_lazy_hz 2 — two ~460 ms stalls per second re-created the 1 fps state; the config now pins 0.
- The shader load phase spends its 1.2 s/frame budget only behind the loading screen; in-game it trickles at 0.1 s/frame.

gl4es stays one launcher argument away (-ref gl4es always wins). Risk noted: the native path had never been field-tested with the current shader set — if Mali's native compiler rejects a permutation, the log's "N failed" count will name it.''')

R['v0.8.6'] = ('v0.8.6 — MSAA force-off: the archived 8x multisampling was the 3fps/artifacts/crash bomb', '''The uploaded opengl.cfg (the renderer's saved settings) carried gl_msaa 1 + gl_msaa_samples 8 — 8x supersampling on a 2-core Mali-G57, silently ignored through the whole gl4es era, honored for real the moment v0.8.5 moved to the native ES3 context.

That one archived setting explains the v0.8.5 field report: 3 fps (eight shades per pixel, bandwidth-starved), artifacts (multisample resolve through the renderer's framebuffer paths), and the crash (8x allocation a budget GPU can't sustain).

Fix: MSAA force-disabled on Android before the EGL configuration is chosen; archived values reset so it cannot come back; the shipped config pins gl_msaa 0. Desktop unaffected; applies to both refs. Note: the attached engine.log was again the stale v0.8.1 file — grab the log right after quitting, or use the crash dialog's bundle.''')

R['v0.8.7'] = ('v0.8.7 — back to gl4es (the proven path) + Session Logs screen', '''Source archaeology round, including a correction of the v0.8.5 diagnosis:

- The gl4es wrapper never changed (pinned since the first port commit); the same gl4es produced the correct v0.6.1 look. The purple era had a local cause: the v0.8.0-83 budgeted compilation drew surfaces with wrong same-family shaders — already fixed in v0.8.1-0.8.2.
- The native ref has now failed four field tests in a row on this device (v0.5.0 garbage, v0.7.0 black terrain, v0.8.5 artifacts, v0.8.6 all-black). The v0.8.1 CPU DXT decoder covers DXT1/3/5 only; the game's BC4-BC7 materials are still rejected on the native path. A material with a rejected texture renders black. gl4es loaded every DDS in every build ever fielded.
- Decision: gl4es is the default again; gles3compat stays opt-in for future testing with telemetry. The MSAA force-off stays.
- New Session Logs screen (launcher Settings): live engine.log + logcat zipped via the share sheet — the plain engine.log kept being a stale file from a crashed session.
- Estimate for this configuration: 25-45 fps (grass, 4-pass mirrors, decals, MSAA now off; none of that was off when 11-18 fps was measured).''')

R['v0.8.8'] = ('v0.8.8 — log fallback + auto share prompt + glass-path diagnostics', '''Root cause of five releases diagnosed without telemetry: the engine opens engine.log with plain fopen() in the game folder; on this device that open fails, the error goes to the log that couldn't be created, and the game runs with no log — every attached file was a fossil from an old session.

- Engine: log-file fallback to the app-private directory (HALFD_LOGDIR), stated on the first line
- Launcher: newest-log lookup across locations with date and path shown; "Share this session's log?" prompt on exit (engine.log + logcat zip)
- Glass-path diagnostics: one line per reflective studio mesh naming material/chrome/cubemap/reflect/bind path — the car-window "self-reflection" repro becomes diagnosable from a log
- Found, not fixed: GL_CreateUberShader never checks link status (a permutation that compiles but fails to link still gets bound)

Also analyzed from the screenshot: the red/magenta grate is most plausibly the normal-map decode decision (GL_EncodeNormal keys on the reported GL format, which gl4es may report differently) — instrumented, not changed.''')

R['v0.8.9'] = ('v0.8.9 — persistent shader binary cache + glass self-reflection fix', '''First real telemetry (v0.8.8 session log): the menu ran 0.7-2.0 fps for ~5.5 minutes, then 13-14 fps; chapter 1 loaded with a 30 s frozen frame, then 12-19 fps. A frame during the slow phase was ~500 ms — ~90 ms scene plus ~400-650 ms of inline shader compiles (mislabeled "cube" by the perf timer). 714 permutations at ~0.45 s each, every launch, because nothing could persist: the game folder isn't writable by the engine and the program-binary cache was disabled under gl4es since v0.4.4.

Fix 1 — persistent program-binary cache: gl4es forwards glProgramBinary to the real driver and introspects the linked program (including the injected _gl4es_* matrix uniforms), so binary-loaded programs keep working; the v0.4.4 crash reason is guarded in our gl4es fork; a binary that doesn't expose the matrix uniform is rejected and recompiled from source. Storage in <app-private>/shadercache/v<version> — version-scoped, stale binaries impossible. Expected: the one-time compile is paid once per app version.

Fix 2 — car glass self-reflection: the truck's glass sampled a cubemap probe containing the truck's own body (static props are baked into probes) through a single-origin reflection vector. If the probe box overlaps the model, the probe is rejected for that model and the glass draws its base texture.

Still open: red/magenta grate lighting (normal-decode logging added), warmed-up scene cost, missing content files (2 vehicle models, ~40 base HL sounds).''')

R['v0.8.10'] = ('v0.8.10 — cache-aware load phase + phase/cube timing split + glass bind-path diagnostic', '''The cache works — 401 of 714 permutations loaded from disk — but two costs remained:

1. The load phase re-loaded every on-disk binary with glProgramBinary, which on this driver costs a large fraction of a compile (~0.4-0.6 s): the cache was paid for twice, every launch.
2. A backgrounded session still rendered sun-shadow passes around the menu preview while the phase compiled at ~450 ms per frame.

- The load phase now skips any permutation whose binary is on disk; first use during play loads lazily
- Heartbeat/watchdog print shphase separately from cube (the old "cube" number included the compile budget — a mislabel that sent the v0.8.8 round chasing cubemaps)
- Binary cache loads are counted and timed with a phase summary line
- Glass diagnostic now names the texture bind path per reflective mesh

Still open: red/magenta grate (normal-format theory weakened — all normal maps report DXT1/plain-RGB), warmed-up scene fps, the car glass visuals.''')

R['v0.8.11'] = ('v0.8.11 — tried-markers (no permutation compiled twice) + cache save-side instrumentation + windshield fix', '''The v0.8.10 log: the on-disk skip works (401 skipped), but the same 313 permutations compiled again (~0.5 s each) and their binaries still never appeared on disk; zero cache loads all session; link failures ruled out (they print loud errors). The save side fails silently somewhere between the driver refusing to serialize and the file write.

- .tried markers: the load phase writes a marker per processed permutation and skips marked entries in later sessions regardless of the save side — no permutation is ever paid for twice
- Save-side instrumentation at warning level: saves, driver-refusals and write failures counted, failed writes print the filename; the phase summary line names the failing path
- Windshield fix: the mod uses a material's fallback texture as a fake reflection when no cubemap is assigned — the menu room sits outside every probe box, so the car photo was painted across the glass. Reflective materials without a cubemap now skip the fallback and draw their own diffuse texture (plain tinted glass)

Still open: red/magenta grate, warmed-up scene fps (54-77 ms scenes), missing content files (truck/sedan models, ~40 sounds).''')

def main():
    req = urllib.request.Request(API + '?per_page=100', headers=hdrs())
    rels = json.load(urllib.request.urlopen(req))
    ok = fail = 0
    for r in rels:
        tag = r['tag_name']
        if tag not in R or R[tag] is None:
            print(f'skip {tag}')
            continue
        name, body = R[tag]
        data = json.dumps({'name': name, 'body': body}).encode()
        req = urllib.request.Request(r['url'], data=data, headers=hdrs(), method='PATCH')
        with urllib.request.urlopen(req) as resp:
            resp.read()
        ok += 1
        print(f'patched {tag}')
    print(f'{ok} patched')

if __name__ == '__main__':
    main()
