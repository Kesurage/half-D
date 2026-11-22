## v0.5.1 — crash fix: the renderer was fine, a 20-year-old extension-string check killed it

Your crash log actually carried good news before the crash: **the native GLES3 port worked** — `Loading renderer: gles3compat`, `GL_VERSION: 3.2 (GLES)`, the menu world loaded, map loading started, shaders compiled. Then:

```
Error: GL_ARB_multitexture is not supported. Custom renderer disabled.
```

One line after the context came up, the game disabled its own renderer. On desktop GL this check looks for the `GL_ARB_multitexture` **extension string**; native GLES3 has multitexture in its core and never prints that string. The check gave up before ever trying the functions — which were all present. With the renderer disabled, the world pointer stayed NULL and the first first-person view calculation hit it: segfault in the weapon-obstacle trace.

**Fixed in v0.5.1:**
- on GLES contexts the extension-string precheck is skipped — the function table alone decides (that's what v0.5.0 already made fully resolvable)
- the weapon-obstacle trace now guards against a missing world pointer, so even a future renderer-init failure degrades to "no weapon pullback" instead of a crash

**Install:** APK over v0.5.0, extract the libs zip over the game folder as before.

**What to look for now:** the game should reach actual gameplay on the native path. Send the engine.log after a few minutes of play — the perf heartbeat lines (`perf: N fps | ... stage ms: scene X`) will show what the native renderer really costs on your Helio G99, and that decides the next optimization round. `-ref gl4es` remains the escape hatch.
