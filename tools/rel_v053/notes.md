## v0.5.3 — one missing function name was holding the whole renderer hostage + the perf watchdog you asked for

Good news first: your v0.5.2 log shows **multitexture passed** (the round-two fix worked) — the failure just moved to the next gate: `GL_ARB_shader_objects`. What you saw as "shaders not loading" and "texture glitches" was the game's renderer still disabled, with the engine's bare fallback drawing the world without any of the game's materials/lighting/effects.

**The bug:** `glDetachObjectARB`. The check that validates shader support tries every function in a 65-entry table, and a single unresolved name disables the entire extension. Stripping the `ARB` suffix from that name gives `glDetachObject` — not a real function (the core name is `glDetachShader`). One name, whole renderer off.

**The fix — and why this can't happen again:** I built an audit that models the client's entire function-resolution chain exactly as the code runs it (native ES3 ∪ shim exports ∪ special-case retries ∪ stubs ∪ suffix stripping) and ran it against **every name in every table the client resolves — 277 total**. Before: 273 resolved. Now: **277/277**. Three more gaps were fixed alongside: `glMultiDrawArrays` (really used by the decal batcher — implemented for real, converting ES3's missing `GL_POLYGON` mode to triangle fans), `glGetQueryObjectivARB` (ES3 only exposes the unsigned read), `glTexEnvfv` (unused; safe no-op). There is no round four hiding in the function tables — this is now proven, not hoped.

**The watchdog (your request):** when a 5-second window averages under 30 fps, the log now prints a `[watchdog]` block: per-stage milliseconds (scene / postfx / texture uploads / cubemaps), draw-instruction and shader-bind counts, polys-per-draw — and a **verdict naming the dominant cost** (world geometry / detail grass / studio models / post-processing / draw-call bound / shader-switch bound / not-GPU-bound) with the exact cvars to turn down. Example of what you'll see:

```
perf: [watchdog] 24.7 fps < 30 | frame 40.5 ms | scene 31.2 postfx 4.1 ...
perf: [watchdog] verdict: SCENE-BOUND, detail grass dominates (31024 grass polys vs 8000 world + 6000 studio) — advice: r_grass 0, r_grass_shadows 0
```

**Install:** APK over v0.5.2, extract the libs zip over the game folder.

**What to expect:** the game's own renderer should finally come up on the native path — shaders, lighting, effects, the real look of the game. Play a few minutes and send the engine.log: the heartbeat lines (`perf: N fps | ...`) plus any `[watchdog]` verdicts decide the optimization round. Your 6 fps expectation on high settings is exactly what the watchdog will confirm or refute with numbers.
