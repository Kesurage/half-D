## v0.6.0 — rolling the renderer back to the configuration that actually worked

You called it: "all comes down to the rendering problem". You were right, and the v0.5.4 report settled which direction is correct. Here's the honest engineering picture:

| build | visuals | fps | menus |
|---|---|---|---|
| v0.4.4 (gl4es) | correct — game shaders, lighting, terrain | 5-15 | working |
| v0.5.4 (native GLES3 experiment) | black ground, error models, artifacts | 0-4 | game + device crash |

**What went wrong:** v0.5.0 bet that running the game's desktop-GL renderer *natively* on your phone's GLES3 (with an automatic GLSL translator + call shim in between) would beat gl4es's translation overhead. Five rounds of capability fixes later, the renderer ran — but the translation cannot reproduce the game's rendering faithfully. Black terrain, broken characters, 0-4 fps, and menu crashes that reset the whole device are all symptoms of that one architectural mistake, not 10,000 separate bugs.

**What I verified against your reference:** I cloned yalnie/Diffusion-Android and diffed it against our tree. Its renderer code contains **zero** GLES-specific handling — the game expects a desktop GL 2.x context, and on Android it runs through **gl4es**. That's the architecture the game's author ships. Our only differences from it were my v0.5.x experiment stack, all of which is now verified inert on the gl4es path.

**The fix:** one architectural rollback — the engine now loads **gl4es first** (correct rendering, working menus) with the native path as automatic fallback. Everything from the working v0.4.4 era is still in place: menu crash fixes, texture fixes, the mobile quality preset (shadows/sun shafts/blur/FSR off by default, grass fades), and the perf instrumentation.

**What to expect:** the game should look right again and menus should not crash. Performance lands back at the v0.4.4 level — roughly 5-15 fps. That is the floor, not the ceiling: the v0.4.4 analysis showed the frame is CPU-bound in translation overhead, which is a real optimization target — but this time it gets attacked with data, not guesses.

**The ask:** play 3-5 minutes and send engine.log. The perf heartbeat (`perf: N fps | ...`) and the low-fps watchdog (`[watchdog] verdict: ...`) now run under gl4es too — they will name exactly which stage eats the frame time (scene polys vs postfx vs texture uploads vs draw-call count), and that log decides the next, data-driven optimization round.

**Note:** if you ever saved custom launcher arguments containing `-ref gles3compat`, remove them (or set `-ref gl4es`) — a saved pin would override the new default.
