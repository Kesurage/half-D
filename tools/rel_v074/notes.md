# v0.7.4 — the launch is fixed, and the rewrite plan is here

You were right, and the data now says so plainly: patching is over. This build does two things — it makes the current renderer **launchable** (the 1fps experience was the shader compile storm, not the renderer's running speed), and it ships the **full rewrite plan** as a document.

## What v0.7.4 fixes (the launch experience)

- **The 1fps session.** With the game's renderer on and no shader cache on this driver, every shader permutation compiled at first draw — a 31-second frozen frame at map start and 9-second hitches mid-game. v0.7.4 compiles the whole list **behind the loading screen** (45-second budget), where a 30-second wait already existed. After load: no more frozen frames from compilation.
- **The eternal "Loading shaders" bar.** Two separate bugs: the bar's done-flag was only set at the end of a frame that slow frames never reached, and it drew even in modes where nothing compiles. Both fixed — the bar can no longer appear stuck.
- **The renderer is pinned to gl4es on the command line.** If your device saved the experimental renderer choice from the v0.7.0 era, it could silently override every later fix and reproduce the old garbage-at-1fps configuration. The command line outranks any saved setting — that whole failure class is gone.
- **Every launch now states its configuration in one log line** (which renderer, which ref, compile mode). No more guessing from symptoms — the next log settles any question immediately.

## What v0.7.4 does NOT claim

The heavy scenes still run at the measured 11–18 fps floor. That floor is architectural — a desktop GL renderer run through a translation layer — and no patch changes it. That is what the rewrite is for.

## The rewrite plan

**half-D-Rendering-Engine-Rewrite-Plan.docx** (next to the APK) is the full staged plan you've been asking for, built on every measurement from the nine device sessions:

- **Stage 1 — Content pipeline:** LOD meshes for the car/NPC models that ship without any (219k polygons at every distance is a data problem, not a GPU problem), re-encoding the artifact-causing textures, shader diet. Gate: worst scene ≥ 20 fps.
- **Stage 2 — Native GLES 3.2 backend (the core rewrite):** gl4es removed from the hot path entirely — batched draws, native shaders, instanced models, zero CPU readbacks. Cut in behind a switch with a frame-diff parity harness so visuals are proven, not hoped. Gate: worst scene ≥ 30 fps with full fidelity.
- **Stage 3 — Systems:** clustered lighting, water on GPU-side buffers, physics at fixed 60 Hz with interpolation — your named root causes, each with its own gate.
- **Stage 4 — Vulkan/panvk:** kept as a measured contingency, not a first move, with the reasons stated honestly in the document.

## What to send after one session

The log (written automatically), the worst scene's name, and a screenshot if you still see artifact textures. With the new banner line, that log is enough to attribute anything that remains.
