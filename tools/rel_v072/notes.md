## v0.7.2 — the sky is back, the log is clean, the 90 fps is untouched

First: **the 90 fps is not going anywhere.** Everything in this build is texture loading and log cleanup — the frame path that gives you 90 fps is byte-identical to v0.7.1. The lag you remember was the custom renderer's shader compilation and draw-call churn; that code is still off.

### What the log actually said (it was nearly clean)

- **40 sound errors** — the game's 1.21 data package doesn't ship those files (NPC voices, footsteps). True on desktop too; nothing I ship can create missing game data. They now print once per session.
- **8 texture misses** — and I traced every one.

### The sky — the big visible fix

Your maps don't set a sky name (the game's own renderer drew its sky with a shader and never needed one). The engine fell back to its default `desert` skybox, those files don't exist in your game data, and the failure was **silent** — the check skips its warning when the name is already the default. Result: the sky was never set up, and sky surfaces rendered with whatever garbage was last bound. That's very likely most of the "gumbled/missing" you saw — the sky is a huge part of the screen.

Fix: v0.7.2 ships a proper skybox inside the APK (a clean Utah-desert gradient: dusty blue zenith, warm hazy horizon — mathematically seam-free on all 12 cube edges, verified 0 pixel difference). The engine finds it at top priority, no code on the frame path changed.

### The noise

- `solid_sky` / `alpha_sky` errors were **cleanup noise** — the engine tried to free sky textures that never existed, and the "free" operation itself did a file search. Fixed: look first, free only if found.
- `Unknown command cl_lod_scale / cl_viewmodel_fov` — those belong to the disabled custom renderer; they're now registered upfront so your config stops warning.

### If any surface still looks garbled after this

Then it's not the sky, and I need to see it: send a screenshot of the garbled area (plus the log). With the sky fixed, anything remaining will be a specific asset I can chase down the same way.
