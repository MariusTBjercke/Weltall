# m02 — See-through scenery, and steadier 3D under the free camera (item 05)

Date: 2026-11-07. Branch `see-through-scenery`, brought into `main`. Build: `Weltall 1.0.1-fork.1`.

## What changed

- **See-through scenery** (Options → Display → See-through scenery: Off / Small / Normal / Large, Normal by
  default). Outside cutscenes, each party member's sprite, or the gear model standing in for it, gets a round
  bubble (`FFieldSeeThroughHole`, `FieldSeeThrough.h`). Rays from the eye to fifteen points of each member (five heights by three across)
  pick the placed models that hide them (`HidesFromEye`), and only when together they hide nearly half of that member
  (`SeeThroughCoversEnough`, 45 %), so a bench before the feet is left alone; the model each stands on is never picked (`BearsSprite`); models
  touching a picked one, above that member's feet and in front of it, are picked with it. A picked model is seen
  through in a 4 x 4 ordered dither within the bubble, only in front of the member and above its floor, after
  0.2 s of hiding and fading in over 0.25 s, out over 0.35 s (`FFieldSeeThroughFade`, kept by `FFieldView`).
- The temporary report of what the field's drawing leaves out (`0e6aa80`) is removed: its causes are fixed.
- **Steadier 3D with the free camera.**
  - `OrderFrame` no longer moves the sprites', particles' and children's entries by the far bucket's shift a
    second time (`def21c9` had added it): the save point was drawn behind scenery and sprites went past the
    far bucket on zoom.
  - Under `EFieldCulling::InRange` the facing is tested in the shader every frame (`FieldPolygonFlags::OneSided`,
    `FFieldPolygonTest::bInRangeAnyFacing`) rather than once a tick against the tick's view: gear parts no
    longer blink as the camera turns. The world's up the shadows test against turns with the view.
  - Against a depth buffer the shadows are drawn after the ground, so a piece of ground sorted nearer by a turn
    cannot paint over half of one.
  - `ModelMargin` is 2 units instead of 8. A probe of all 67 gear models found 8 details laid over another
    polygon of their part, every one exactly in its plane, so a wider margin only let parts a few units apart
    be drawn in the order, which a turning camera flips.

## Verified

- Core: `ctest --preset windows` — 1991 tests, the 19 known aborts of `.agents/tests/expected-aborts.txt`
  failing and nothing else. One existing test changed its rule on purpose (FieldSceneTests: the polygon facing
  away now stays in the order under `InRange`).
- Rendering: `Automation RunTests Weltall` — 81 of 81 `Result={Success}`, among them
  `FieldSeeThroughShowsTheParty`, `FieldSeeThroughShowsThePartysGears`, `FieldSeeThroughOnlyWhatHidesTheParty`, `FieldSeeThroughNotWhatHidesTooLittle`,
  `FieldShadowsStayOnGroundDrawnAfterThem` and the gear depth test at 4 and 1 units.
- Package: `.\Scripts\build.ps1` — built, and the 60-second check passed.
- Played by the owner from `D:\Games\Weltall` through each step: the cave with Weltall and Brigandier, a
  bridge, platforms and jumps; the normal size was accepted. The last steps, the choice of size and the coverage
  a model needs (after a bench in a house), await the owner's final play.

## Known gaps

- Scenes staged before a painted picture get no bubble.
- The world map and battles are untouched.
- A map that merges much into one large model sees all of it through when a part of it hides the party.
