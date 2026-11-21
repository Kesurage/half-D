## v0.4.1 — found it: the options menu was re-arming the crash

Your v0.4.0 log confirmed all four previous fixes went live (no more shadow-texture errors, menu font fixed, shader cache enabled, precompile ran). This round it finally exposed the actual crash trigger and three more mechanisms.

### The crash when selecting a graphics option — root cause found & hard-fixed
The **video options presets re-enable every expensive effect**: selecting any graphics quality option sets `r_bloom 1`, `gl_sunshafts 1`, `r_blur 1`, `gl_hbao 1`, `gl_water_refraction 1`… — each of those turns a **per-frame fullscreen screen-capture** back on. Through gl4es on your Mali that is a CPU readback (the same cliff as FSR/DownScale/the menu blur): multi-second frames, and the Android GPU watchdog then kills the game and destabilizes the device.

The v0.4.0 menu-blur fix only covered *one* of those effects. This build adds a **hard gate on all 11 capture effects** — on the GL2 path they stay off **no matter what the options menu or saved config says**. Selecting options can no longer re-arm the cliff. (`gl_mobile_quality 0` in console opts back in if you ever want the full effects.)

### The 279 failed shaders — explained, filtered, and now logged
- 169 `HAS_SHADOWS` + 196 `SUN_SHADOW` shader permutations use shadow samplers that **gl4es cannot translate at all** (its ES fallbacks don't apply because gl4es presents desktop GL). They can never work on this device — the precompile now **skips all ~413 impossible permutations**, so first launch compiles ~700 instead of 1111, faster and without the failure noise.
- Failed shaders were retried **every frame** if anything requested them (each retry = a full driver compile = a stall). Failures are now remembered and never retried.
- All shader failures are now **always logged** (they were invisible — the engine's error printer is disabled without developer mode). If anything still fails, the next log will name it and say why.

### The 25-second load — thief hunt instrumented
The v0.4.0 stage timers proved the 25 s is **not** in any of the 17 BSP heap stages. Every remaining phase is now timed (file read with size, lump byte-swap, BSPX scan, raw lump loading, studio-cache purge) — the next log will name the thief directly.

### Rain texture fixed
Your log confirmed `raindrop` is **BC7 SRGB**, which Mali cannot decode via gl4es (no BPTC). The libs zip now ships an uncompressed replacement — **this is why extracting the new libs zip over the game folder matters** (it overrides the broken one).

### Also in this build
- Perf heartbeat now reports **draw calls / passes / shader binds** — tells us whether the ~100 ms scene stage is draw-call-bound or geometry-bound (decides the next optimization).

**Install:** APK over v0.4.0 **and** extract the new libs zip over the game folder (the raindrop fix lives there).

**Test specifically:** open Options → select graphics quality options → enter/leave menus repeatedly. Then play a session and send `engine.log`. Expected: no crash, no multi-second spikes after the shader load, and the log will contain `load:` phase lines + `dips/passes` in the perf lines.
