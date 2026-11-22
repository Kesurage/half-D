# v0.7.3 — the full renderer is back, and the fake loading bar is gone

You were right on both counts, and the two things you saw were connected.

## What "Loading shaders... 0/0" forever actually was

A bug, not a real load. The shader progress bar is drawn by the game's HUD code every frame until the shader compile pass reports "done". But the compile pass only runs inside the **custom renderer's** frame path — and since v0.7.0 that renderer was disabled (performance mode). So nothing was ever loading: the bar was a leftover drawn over a frame that never compiles anything. Fixed — the bar now only appears when shaders are genuinely being compiled.

## Why the world looked broken — the honest answer

The 90 fps was real, but it came from **not running Diffusion's own renderer**. The engine's built-in renderer has none of Diffusion's tech: no shader materials, no dynamic lighting, no effects, and no support for the PrimeXT prop format — that's why the cars came out oversized with missing components and lighting/shaders/effects were simply absent. No amount of patching fixes that; it's what "skip the game's renderer" means.

The v0.6.1 test data proves the game's own renderer draws this game **correctly** — cars, terrain, menus, dusk sky — at 11–18 fps in the heaviest scenes, and the 1 fps disaster back then was the shader compile storm, which is fixed.

## What this build does

- **The full Diffusion renderer is the default again** (materials, lighting, effects, correct props).
- **Expect one long first map load** — shaders compile on first use, ~0.1–0.5 s each, once per launch. Let it sit; it's working, not hung.
- After load: the mobile quality preset is active (shadows/sunshafts/grass tuned down for the Mali GPU).
- If you ever want the 90 fps speed run back: console, `gl_diffusion_renderer 0` (the setting is saved).
- The skybox fix from v0.7.2 stays in.

## What to send me next

The perf heartbeat is on by default. After one session: the log (`-log` writes engine.log), plus which map/scene felt worst. The known floor is the big outdoor scenes (219k+ polygons in the car models with no LOD data) — that's the next optimization target, with real numbers this time.
