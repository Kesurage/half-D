## v0.5.2 — the multitexture veto, round two + a real engine-side overflow fix

Your second crash log told two stories at once.

**Story one — why the custom renderer was STILL disabled.** My v0.5.1 fix worked (the log printed `GL_VERSION: 3.2 (GLES)` — the context detection did its job), but the multitexture check failed one branch deeper: with `cvar_from_engine`, the game client asks the *engine* for its `gl_arb_multitexture` cvar instead of deciding itself. The engine's native GLES renderer never probes the desktop `GL_ARB_*` names, so that cvar is missing (or archived 0 after earlier failed probes) — and the check returned "not supported" without ever trying the function table, where everything resolves. Fixed: on GLES contexts the capability cvar value is ignored; the function table alone decides. Also added the ES3 fallback for the texture-unit limit query (`GL_MAX_TEXTURE_UNITS` is a fixed-function ES1 enum that native ES3 rejects — the engine already had this fallback, the client now does too).

**Story two — the actual crash.** With the custom renderer off, the engine's fallback renderer took over (it drew the whole world fine — a nice validation of the native ES3 path) and then died in `R_StudioBuildArrayFloatMesh` writing a studio mesh batch past its fixed 16384-vertex buffer. The game's high-poly models can expand past that mid-batch. All three mesh builders (normal/float/chrome) now truncate a batch at the buffer limit instead of writing past it.

**Net expectation for this test:** the game's own renderer should now initialize on the native path — menu, world, models, shaders. If anything still fails, the log will say exactly which capability, but this time every decision point on the ES path goes through the function table that was fully validated offline.

**Install:** APK over v0.5.1, extract the libs zip over the game folder.

**Send me the engine.log** after a few minutes of play — the `perf:` heartbeat lines decide the next optimization round. If it crashes again, the zip as usual; the stack will now be somewhere genuinely new, which is progress each time.
