## v0.5.0 - the renderer port: Diffusion now runs NATIVELY on your phone's GLES 3.2, not through a translation layer

You were right that swapping small pieces wouldn't get there. Your v0.4.4 log proved it: 5-15 fps with the frame stuck 80-160 ms inside the CPU translation layer, and lowering resolution changed nothing (it was never fill-bound). So this build changes the architecture.

**What changed:** until now the game's GL2 renderer was fed through **gl4es** - a desktop-GL-to-GLES translation layer that re-processes every GL call on your CPU. v0.5.0 ports the renderer to run **natively on the Mali-G57's GLES 3.2 context**:

- Every shader is rewritten to native GLSL ES 300 at load time. The whole shader surface - all 1141 permutations - was compiled and link-validated offline before shipping: zero failures
- Fixed-function matrices and vertex streams are injected as real uniforms/attributes, tracked by the engine
- The GLES3 renderer is now the default on Android; **gl4es stays as an automatic fallback** - if the new path fails to start, the old one loads
- **Shader precompile is back** - from source, behind the loading screen. The 13-76 second mid-game freezes from v0.4.4 (lazy compiles during play) are gone. First launch of each map shows a "Loading shaders..." phase - that is the cost moving where it belongs
- Grass (41K quads in your menu scene!) and other quad draws are converted to native triangle draws

**What to expect:** the 80-160 ms CPU translation overhead is what this build removes. Post-processing stays off in the mobile preset - turn effects on in the graphics options to taste; on the native path they actually can run. Report what they cost.

**Install:** APK over the old one, then extract the libs zip over the game folder.

**If anything misbehaves:** add `-ref gl4es` in the launcher's command-line arguments to return to the previous renderer, and send the engine.log either way. Look for `GL_VERSION: 3.2 (GLES)` near the top of the log - that line means the new native path is active.

**Known limitations on the new path:** decals on character models may render without the shader-blended look; shadow maps compile but are the least tested feature; mirrors/portals are worth a second look.
