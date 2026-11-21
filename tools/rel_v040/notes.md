## v0.4.0 — the log finally talks: menu crash fixed, dynamic lights restored, shader cache on

Your v0.3.0 `engine.log` pinned every remaining symptom to an exact mechanism in code. Four independent bugs, all fixed:

### 1. The crash when opening options / any menu — FIXED
The renderer has a **"blur when paused (menu is shown)"** effect that `r_blur 0` never gated (it only gates water/sky/damage blur). While any menu was open it captured the whole screen **twice per frame** (`glCopyTexSubImage2D`) — through gl4es on the Mali that is a CPU readback, the same cliff as the FSR/DownScale rounds: 0.5–13 s frames, and the Android GPU watchdog then kills the game (and can take the device down with it). New `r_menu_blur` cvar, off in mobile preset v6. Every other screen-capture effect was audited — all properly gated.

### 2. Lighting issues / dark areas / glitchy characters — FIXED
gl4es exposes no `GL_ARB_depth_texture`, so the renderer's shadow-stub texture was never created — and **every dynamic light pass aborted with `shadow texture for the light is missing`** (visible in your log). Dynamic lights contributed *nothing*. Shadowless lights now get a plain white stub and render normally (without shadows — real shadowmaps are impossible without depth textures).

### 3. "Couldn't precompile shaders" + 1–13 s frame spikes — FIXED
The shader-binary extension check demanded `glProgramParameteri(ARB)`, which gl4es doesn't export (it does export the binary load/save). That silently disabled the **shader disk cache and the whole precompile pass** — every shader permutation compiled lazily mid-frame (your 12911 ms spike). The cache now works: the first launch compiles all permutations behind the game's own "loading shaders" screen (this will take a while — let it finish), and every launch after loads them from `.shadercache/` in the game folder.

### 4. Console font — FIXED
The menu maps its console font to `tahoma.ttf`, which the itch package does not ship. It now uses the bundled FiraSans.

### Diagnostics in this build (for the next log)
- **BSP load-stage timers** — your 25 s map load will now print exactly which stage eats it (`load: textures in 18.2 s` style)
- caps line now reports `bptc/rgtc/program_binary` — your `raindrop` texture fails upload, almost certainly BC7 which the Mali can't decode via gl4es; the new line will confirm
- texture upload failures now name the pixel format

### Still known (not port bugs)
- Trucks, sky textures, glass particles, HL1 pain sounds = missing files in your game data
- Menu background map renders heavy studio geometry (~100 ms scene) — usable now that the blur is gone, optimization is a next-round target if the log asks for it

**Install over v0.3.0.** After playing, send `engine.log` — the new load-stage lines, caps line, and the absence (or presence) of shadow-texture errors are exactly the data that decides the next round.
