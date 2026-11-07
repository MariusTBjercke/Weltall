# m01 — Message boxes at their own scale, and opaque (item 04)

Date: 2026-11-07. Build: `Weltall 1.0.1-fork (b3d017b)`.

## What changed

Two rows under **Options → Display**, both leaving the original's look until the player changes them.

- **Text box size** — `Original` / `Whole pixels` / `Smaller`. The message windows' primitives are now recorded
  in `FFieldFrame::MessageRange` and drawn in a pass of their own (`FFieldScreenFrame::AddPasses`), at a whole
  number of display pixels a pixel instead of the picture's fraction of one. They stay centred, and their
  bottom sits where the picture's own scale put it. `Smaller` is that whole number one step down.
- **Text box shading** — `Original` / `Solid`. `Original` is the kernel's subtract, which darkens the scene
  behind the box but lets bright things through (the field's compass). `Solid` draws the background opaque
  instead (`FFieldPresenter::SetSolidMessageBox` → `BuildFieldScreenLayer`).

The rest of the screen keeps the scale it had: the 3D scene, the field's sprites, the fades and the screen
markers are untouched, and the split only happens when a window is actually open.

## Verified

```console
$ .\Scripts\build.ps1
Exporting b3d017b into out\package\Weltall
  BUILD SUCCESSFUL
Development: built in 1 min: out\builds\Development\Windows\Weltall.exe
Development: ran for 60 s (log: out\builds\Development-check.log)
```

```console
$ ctest --preset windows   (Tests/)
99% tests passed, 19 tests failed out of 1983
```

The 19 are the known aborts of `.agents/tests/expected-aborts.txt` (they SKIP with exceptions off, which
terminates them). Two tests failed on the first run and were fixed rather than worked around:

- "Every row's words are in the game's font and fit their places on the options screen" — one new help line
  was 248 pixels against a 244 limit; the wording was shortened.
- "The options' choices wrap, and their numbers step within their ranges" — it walks the Display page by row
  index, so two new rows moved the rows after them by two; the indices were updated and the two new rows got
  assertions of their own.

## Not proven

**The only proof of this change is playing a dialogue scene.** The start-up check boots the game and reaches
the field program, but no message window is open during it, so neither the split pass nor the background runs
there. Nothing here has been seen on screen yet.
