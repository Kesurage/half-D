# Controls Plan — Diffusion's custom scheme on touch

## The problem

Diffusion is **not** a standard Half-Life mod in terms of interaction. Per the itch.io page and community discussion, it has no official controller/touch support and uses custom interaction logic (contextual actions, custom HUD and weapon handling, vehicle sequences). The stock FWGS touch layout (move stick + look area + attack/jump/crouch/use buttons) assumes vanilla GoldSrc binds. Our job is to translate Diffusion's scheme — not force the stock layout onto it.

## Approach

1. **Inventory the custom binds.** Extract every `bind`/impulse and custom client command Diffusion uses (from its `config.cfg`, `user.scr`, liblist and client DLL source in Aynekko's Diffusion repo). Produce a table: action → engine command → touch widget candidate. Store in `docs/bindings-table.md` (Phase 3 deliverable).
2. **Keep it custom, per the mod.** Where Diffusion replaces standard actions (e.g. contextual interact, weapon-specific ability), the touch UI gets a dedicated adaptive widget rather than a generic button:
   - **Contextual interact button** that relabels itself based on the current HUD hint state (via engine's client messages / `hud_` hints).
   - **Weapon wheel** instead of the number-row: FWGS already has a `_wheel` touch control type; bind it to Diffusion's weapon switch logic.
   - **Quick-action cluster** for the mod's ability/stance commands, positioned for thumb reach.
3. **FWGS touch engine extension.** FWGS Android already supports editable touch buttons (`touch_` cvars, layout files). We ship a **default `diffusion.layout`** tailored to the mod, plus an in-app editor (engine already ships one — restyle to match launcher).
4. **Gyro aim** (optional): map look axes to sensor via engine Mobility API; toggle + sensitivity slider.
5. **Gamepad first-class:** the community already proved gamepad configs work with Diffusion on PC; ship a mapping profile for common Android gamepads.
6. **Vehicles:** vehicle sequences get a second layout page (steer left/right, accel/brake) auto-swapped when a vehicle is entered (detect via client state).

## Testing checklist (per chapter)

- [ ] Chapter 1: all scripted interactions reachable with touch only
- [ ] Weapon switching fast enough for combat (wheel < 1s)
- [ ] No action requires more than 2 simultaneous thumbs
- [ ] HUD text readable at phone size (scale settings)
- [ ] Vehicle sections playable
