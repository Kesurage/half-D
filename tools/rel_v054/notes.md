## v0.5.4 — your renderer came up and started compiling shaders; one illegal line in my prelude stopped it

You're right to be frustrated by the iteration count, so here's the honest frame: **this crash is the deepest one yet, and it isn't a segfault** — it's the game's renderer initializing on the native path for the first time, compiling the actual game shaders, and stopping cleanly with a precise error: `P0001: Extension directive must occur before any non-preprocessor tokens`. That error text let me pin it to the exact line.

**The bug (mine, not the game's):** when my ES compatibility layer builds each shader, it prepends a compatibility prelude — and that prelude ended with `#extension GL_EXT_gpu_shader4 : require`. Two faults in one line: that extension doesn't exist in GLES3 at all (its features are core there), and directives must come before declarations — your Mali GPU enforces the rule, every desktop driver I could test against silently tolerates it. The game's own shaders are innocent (they contain zero extension directives).

**Why my offline validation missed it:** the validator was still modeling the old gl4es-era device (no shadow samplers, no texture arrays) — but the capability fixes from the last two rounds turned all of those ON on your phone, so the device was compiling shader permutations my validator had never checked, with a prelude line it didn't model. That's on me.

**What I did about it:**
- the desktop-only directive is no longer emitted on GLES (the feature it gates is core in ES3; the shader code keeps working)
- the offline validator now models your **actual** device profile: all **1141** shader permutations compile **and link** as strict GLSL ES 300 — including every shadow and terrain-layer permutation that's now live — plus a rule check that no extension directive can ever follow a declaration. Result: **1141/1141 clean**, not 1005-of-1141 like before.

**Install:** APK over v0.5.3, extract the libs zip over the game folder.

**What to expect:** this is the first build where every layer the phone exercises has been validated against the exact conditions it runs under — capability negotiation, function resolution, and now every shader permutation. If it runs, play a few minutes and send the log (the `perf:` lines and any `[watchdog]` verdicts decide the optimization round). If it stops again, the log will name the exact shader and error — same drill as today, but we're out of known unknowns.
