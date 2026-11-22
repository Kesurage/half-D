## v0.6.1 — your log cracked it: three fixes, one APK, no more libs zip

Your engine.log was the breakthrough — it rewrote the whole picture, and it says **no rewrite is needed**. Here's what it actually contains:

**1. The renderer is CLEAN — zero shader failures.** The "99+ errors" are missing **game data**, not renderer bugs: ~65 sound files (NPC pain/attack voices, footsteps, fvox beeps) and 2 car models (`sedan_volkswagen_passat.mdl`, `truck.mdl`) that the Diffusion 1.21 package simply doesn't include. I checked the author's own source repo — it ships no data files, so these gaps exist for everyone. They're cosmetic (silent NPC voices, missing car on two maps). Check your game folder — if `sound/hgrunt/` is missing there, re-extract the 1.21 package.

**2. The "takes forever to load" — found and fixed.** Every launch was compiling **714 shader permutations from source** because gl4es has no shader binary cache to save results — your log shows the precompile banner printed **369 times**, each pass costing 1-2 **seconds**, holding the menu at 0.5-1 fps for 6+ minutes. And since nothing persists, it repeated on EVERY launch. The fix: skip that pass entirely on drivers without a binary cache — exactly what the author's own Android port does. Shaders now compile invisibly on first use (~100-500 ms each, once per session).

**3. The 0-10 fps in-game — the main fix: LOD was OFF.** The game has a built-in level-of-detail system (distant models swap to coarser meshes) that defaults to off on desktop. Your phone was rendering **1,050,000 polygons per frame** in the menu world and 74,000-2,250,000 in-game — every car and character at full detail at any distance. v0.6.1 turns LOD on (author's own system, author's own settings) under the mobile preset. Models without LOD meshes are unaffected.

**4. No more downloading the libs zip.** I verified the APK is fully self-contained — game libraries, all 74 shaders, particles and fonts are bundled inside it (your log confirms the game loads them from the APK, and they override anything in your game folder). From now on: **install the APK, done.** Nothing to extract, ever. One file per update.

**What to expect:** the menu should be usable immediately (no 6-minute compile), load times way down, and in-game fps should rise meaningfully — the watchdog verdicts will tell us exactly how much (and what's next) from your next log.

**The ask:** play a few minutes, send engine.log. If fps is still under 30, the `[watchdog]` lines now point at the next bottleneck with numbers.

**Why not the rewrite?** Because the evidence doesn't ask for one: shaders 100% clean, rendering correct, crashes gone. What remained were two config mistakes (LOD off, precompile on) and missing data files. A rewrite would discard a working, correct renderer to chase bugs we've already named and fixed.
