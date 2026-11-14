# Gear floor shadows painting up the feet

Question: the round shadow under a 3D gear on the field cut straight across its boots — the shadow
looked like it lay halfway up the feet instead of on the ground. Why, and what keeps it under them?
Investigated 2026-11-14 against the fork on upstream 1.0.3. Fixed the same day.

## How a floor shadow is drawn (verified)

- A gear's shadow is a quad from the model's own shadow object: `DrawShadow` in
  `FieldGearDrawing.cpp` places it from `FGearFrameShadow`, and `FFieldSceneMesh::AddGears`
  (`FieldGearDraw.cpp`) gives it the sprites' blob texture and `FieldPolygonFlags::BroadShadow`.
  Characters on foot get their 48-unit quad from `FFieldFrameCapture.cpp` (`CaptureShadows`); actors
  with `EventActorFlags1::Gear` are skipped there, so a gear's blob is the gear's own.
- `DescribeGear` marks it from the shadow blend (`bShadow`, not `bModel`), and
  `FFieldDrawBuilder::Depth` draws every shadow **last**, after the sprites and models have marked
  the stencil and written depth (`EFieldRunDepth::OutsideMask`).
- `DepthStatesOf` draws the shadows twice: **outside all marks** with `CharacterShadowHeight` (64)
  or `GearShadowHeight` (256) above the floor, and **over a model's marks only** (never a sprite's)
  with `OverModelShadowHeight`. The heights make a shadow hide behind whatever stands higher than
  them over the same floor.
- The vertex shader (`FieldScene.usf`, `MainVS`) tests the shadow at that height above its floor:
  `Depth = View.z * max(1 - ShadowHeight / max(Above, ShadowHeight), MIN_SHADOW_SHARE)`. The same
  shader serves the field and the battle scene.

## Why it reached up the boots

The over-model pass is deliberate: it darkens the pixels of the model standing in the shadow, so the
soles lie in it and the gear does not look pasted on the floor. But its height is a **world-space**
constant, and the field's stand-ins are scaled to the sprite they replace ( `FieldGearStandIns.cpp`:
`Scale = SpriteHeight / ModelHeight`). A gear's foot is therefore only some ten world units tall, so
a 16-unit band covered the whole boot and part of the shin — a hard edge across the foot.

Measured on `FFieldGearShadowRenderingTest`'s synthetic gear (110 units tall, sole 3 units above the
floor, one pixel row per 1.8 units): with `OverModelShadowHeight` at 16 the band ended at row 159
(~16 units up); at 8 it ended at row 163 (~8); at 4 at row 165 (~5), while the sole at row 166
(3 units) stayed in shadow.

## Fix

`OverModelShadowHeight` 16 → 4 in `FieldSceneRendering.cpp`, and a regression assertion in
`FieldGearShadowRenderingTest`: row 162, ~10 units above the floor, must show the gear and not the
shadow. The full editor suite passes, 85 of 85, including the battle test that wants the sole 2
units above the floor in its shadow and the legs at 26 units in front of it.

## Still open

- The band is still a fixed world height, not a fraction of the model. Four units is the sole for
  the gears and children's models seen so far; a much smaller stand-in would take it further up its
  feet. Scaling it per model means the height has to travel with each shadow run.
- `CharacterShadowHeight` and `GearShadowHeight` are unchanged: they only act outside all marks, so
  they cannot paint over a sprite or a model.
