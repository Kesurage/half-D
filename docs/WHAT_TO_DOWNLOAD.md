# What to download

## You (the player/device)

1. **Diffusion v1.21 — itch.io (canonical, latest)** — https://aynekko.itch.io/diffusion
   - ~2.8 GB zip. This contains the `diffusion/` game folder (maps, models,
     sounds, voices) **plus the `glsl/` shader folder**. Itch.io is the
     developer's primary host; version there is v1.21 (July 2026).
2. **ModDB mirror** — https://www.moddb.com/mods/diffusion
   - Use only if itch is slow; check the ModDB version matches v1.21 (ModDB can
     lag behind). Don't mix versions: shaders and content must come from the
     same release.
3. **No Steam / Half-Life purchase needed** for the standalone path — the itch
   package is self-contained. Caveat: Xash3D-FWGS sometimes asks for a few base
   `valve/` assets; if the engine complains on first run, copy a minimal
   `valve/` folder from an owned Half-Life copy onto the device. We will verify
   exactly what's needed once the APK boots (will be recorded in
   `logs/device-tests/`).
4. **The APK and the Android game libraries** come from the
   [Releases page](https://github.com/Kesurage/half-D/releases) — start with
   Release v0.1.0 (`half-D-engine-apk` + `diffusion-android-aarch64-libs`).
   CI artifacts on the Actions tab need a GitHub login and expire; Releases
   don't. Nothing else to download.

## amd64 vs arm64 — why the download alone can't run on the phone

The Diffusion download ships **PC binaries**: Windows x86/x64 DLLs and a Linux
**amd64** build. Your phone (Helio G100) is **ARM: arm64-v8a** — those binaries
cannot run on it, which is exactly why this fork exists: the game's *source*
(vendored in `game/`, GPL-3.0) is recompiled for arm64 by CI. What carries over
from the itch.io download untouched is the arch-independent content: maps,
models, sounds, voice, sprites — and the `glsl/` shader folder (must match the
game version). Our `diffusion-android-aarch64-libs` zip replaces only the
binaries: `diffusion/bin/client_android_arm64.so`, `server_android_arm64.so`
(PhysX inside), `menu_android_arm64.so`.

## Where files go on the phone (Phase 2 will automate this)

```
<engine storage dir>/diffusion/     ← the game folder from the itch zip
<engine storage dir>/diffusion/glsl ← from the same zip (must match version)
<engine storage dir>/valve/         ← optional fallback, only if engine asks
```

The engine launcher points at the storage dir; exact paths get finalized with
the standalone launcher in Phase 2.

## Development machine

Nothing. GitHub Actions builds both artifacts. Local build only if you want it:
JDK 17+, Android SDK (platform 35, build-tools 36.0.0) + NDK r29
(29.0.14206865), Python 3, git — then `scripts/` + `.github/workflows/build-apk.yml`
steps are the recipe.

## CI build inputs (auto-fetched, listed for transparency)

- SDL2 2.32.10 tarball (engine Java layer)
- FWGS FFmpeg prebuilts (engine video codecs)
- PhysX 4.1 @ EmbarkStudios/physx-rs commit `6b3a945` (server physics)
