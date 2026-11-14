# The party and its shadows a frame behind the camera

Question: turning the free camera, the shadows (and the party's sprites) seemed not to follow the
camera's frame rate — a small judder a tick behind the ground. Investigated 2026-11-14 against the
fork on upstream 1.0.3. Fixed the same day.

## How a field frame is drawn between two ticks (verified)

- The field ticks at 30 Hz; the display shows 60 frames or more a second. `FFieldView::Capture`
  (`FieldView.cpp`) builds the draw from the latest frame with `BuildFieldFrameDraw`, which blends
  the scenery's and the gears' object matrices between the two frames' views by `Alpha`.
- The characters' quads, the particles' and the children's models' are **not** blended the same way:
  `AddFieldCharacterQuads` and its neighbours (`FieldFrameGeometry.cpp`) take corners **already in
  view space**, made with the latest frame's view, and place them on the one identity object
  (`FFieldSceneDraw::ViewObject`). Their own movement between frames is interpolated by
  `FieldCharacterOffset`, but the **view** they were made with is not.
- After building, `UnroundFieldSceneView` moves every object from the frame's rounded view to its
  unrounded one, and `Capture` then moves everything from the view the frame was *shown* with
  (`Drawn`, the offsets interpolated between the two frames) to the player's live offsets. The
  scenery's objects start at the blended view, so that move lands them on the live view exactly.
  The view-space quads start a whole frame ahead of the blend, so they landed on the live view
  rotated by the remaining `(1 - Alpha)` of the frame's camera turn: up to one tick of the turn,
  seen as the shadow hitching behind while the ground glides.

## Fix

Two parts, and the first alone made it worse — worth knowing before trying it again:

1. `MoveFieldSceneQuadView` (`FieldFrameDraw.cpp`, declared in `FieldSceneRendering.h`) moves only
   the view-space quads' object; `FFieldView::Capture` calls it with the latest frame's view and the
   blended view before unrounding, so the quads and the scenery share the same view.
2. But `FieldCharacterOffset` (`FieldFrameGeometry.cpp`) had been interpolating the quads by the
   difference of their **view-space** `Origin`s, which moves with the camera as well as with the
   character. So the camera's turn was already in the offset, and the view blend added it a second
   time: the shadow shook harder than before. The offset now recovers the character's **world**
   positions from the two frames' origins (inverting each frame's view) and returns only its own
   movement, in the latest frame's view. With a still camera that is exactly what it was.

The gears' models and their own floor shadows are scenery-like objects, blended already, and were
never affected; a mod's 3D gear sprite (a sprite model) and any character shadow under it are the
view-space quads, and those are what this fixes.

`FFieldViewMoveTest` now also checks that the quads' move takes their object alone, leaving a placed
model's rows as they were. The full editor suite passes, 85 of 85.

## Still open

- The quads' blended view is the straight interpolation of the two frames' matrices, as the scenery's
  blend is; a cut between frames is skipped by `bBlended`, as before.
- The character's world position comes from inverting the frame's view; the view is a rigid scale and
  translation, so that is exact up to the float conversion the renderer already does everywhere.
