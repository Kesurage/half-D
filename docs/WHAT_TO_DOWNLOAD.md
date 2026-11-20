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
4. **The APK** comes from our GitHub Actions (artifact `half-D-engine-apk`), and
   the game libraries from artifact `diffusion-android-libs`. Nothing else.

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
