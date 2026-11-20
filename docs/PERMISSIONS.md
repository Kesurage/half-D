# Permissions & Licensing

## Status

| Component | License | Permission status |
|---|---|---|
| Diffusion (game, code + assets) by Aynekko | **None stated (all rights reserved)** | Verbal permission from the developer to fork and port obtained by the project owner. **TODO: capture written permission (DM/email screenshot) specifying scope: redistribution of assets in APK/OBB, modification of game code, GitHub publication. Store proof in this folder (private until confirmed public-safe).** |
| Xash3D-FWGS engine | See upstream `LICENSE` (https://github.com/FWGS/xash3d-fwgs) | Forking permitted; preserve upstream license, notice and attribution when we vendor `engine/`. |
| AMD FSR 1.0 shaders | MIT | Port to GLSL ES with attribution header retained. |
| half-D original code (tools, launcher, patches) | MIT | Ours. |

## Rules for this repo

1. **No Diffusion assets committed** until written permission is on file. Content ships via OBB built locally by the maintainer.
2. Keep upstream engine license text intact in `engine/`.
3. Any borrowed WinlatorMaliPlus technique (BCn transcode pipeline, panvk packaging format) must keep its upstream license header; that project mixes LGPL (Wine) / MIT (Box64) components — we reference implementations, not vendor their code, unless license review clears it.
