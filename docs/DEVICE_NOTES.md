# Device Notes — Infinix Hot 50 4G (Helio G100)

## Hardware

| Item | Value |
|---|---|
| SoC | MediaTek Helio G100 (6 nm) |
| CPU | 2× Cortex-A76 @ 2.2 GHz + 6× Cortex-A55 @ 2.0 GHz, aarch64 |
| GPU | ARM Mali-G57 MC2 (Valhall, 2 cores) |
| RAM | 4/8 GB (SKU dependent) |
| Display | HD+ (720-class), up to 120 Hz |
| OS | Android 14 (XOS) |

## Graphics expectations

- GoldSrc-class rendering is fill-rate and draw-call light; the A76 pair is more than enough CPU for Diffusion. The Mali-G57 MC2 handles GLES 3.0 comfortably at 720p with FSR1 at 50–75% internal scale.
- **No native BCn sampling on Mali GLES** — Diffusion textures in DXT/BC formats must be transcoded to ASTC/ETC2 at packaging time (see BUILD_PLAN Phase 4).
- Vendor GLES driver is the default path. panvk (Mesa, supports Valhall; on Android via panvk-kbase-android against the `mali_kbase` kernel driver) is an optional experiment only.
- Thermal: budget phone in a plastic chassis — sustained loads throttle. The "Balanced" preset (65% FSR scale, 60 fps cap) is the default for this reason, with auto-downscale on thermal warnings.

## Benchmarks to capture (Phase 1, stock HL)

- fps at 100% / 75% / 50% internal resolution, `cl_showfps 1`
- Level load time (chapter start)
- Frame time p99 during combat scenes
- Battery drain per 30 min session

Log all runs in `logs/device-tests/`.
