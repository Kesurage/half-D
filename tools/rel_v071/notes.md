## v0.7.1 — crash fixed, performance mode now runs on the proven path

Your crash log told the whole story in two parts — both fixed:

**1. The crash (SIGSEGV at 0x0).** My v0.7.0 gate disabled the custom renderer *too early*: the client's GL function pointers were never resolved, so the first time the crosshair HUD tried to draw (right after you switched weapons — the texture-load burst at the end of your log), it called a NULL function pointer and died. Fix: the function table is now resolved before the gate — the same state your device already ran fine in earlier builds. The crosshair calls now hit real (harmless) GL entry points instead of NULL.

**2. Why textures went missing in v0.7.0.** Your log showed *every* game `.dds` (health icons, crosshairs, scopes, use icons) failing to load under the native GLES renderer I switched to — while the identical requests all succeeded under gl4es in v0.6.1 (I diffed the logs: same mounts, same files, only the renderer differs). This also finally explains the "black terrain" from the earlier native experiments — the engine's texture path fails on this game's DDS data under that renderer. So **performance mode now runs on gl4es** — the proven texture path — minus the custom renderer (which was the measured cost driver: compile storms, 1000+ draw calls).

**Net configuration:** engine renderer (fast, batched, no shader compilation) + gl4es (proven to load 100% of your game's textures). The full shader look stays available: `gl_custom_renderer 1` in the console.

### What to expect
No crash on weapon switch, textures back, no shader-compile stalls, and the frame time that the custom renderer was spending on 1000+ shader-bound draw calls now goes to the actual scene. If anything's still off, the log will say — send it as always.
