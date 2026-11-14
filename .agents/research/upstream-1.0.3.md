# Taking upstream 1.0.2 and 1.0.3

Question: the fork was made from the `v1.0.1` release on 2026-11-05 and has 42 commits of its own.
Upstream shipped 1.0.2 on 2026-11-09 and 1.0.3 on 2026-11-11. Is taking them worth the merge?
Investigated 2026-11-14 against `upstream/main` at `03452af`. No code was changed for this note; the
trial merge was done in a scratch worktree and thrown away.

## Where the two trees stand

| | Value |
|---|---|
| Merge base | `9344d23`, the `v1.0.1` tag (the fork's whole history sits on the release) |
| Fork commits since | 42, three of them not yet pushed |
| Upstream commits not in the fork | 2 (`Weltall 1.0.2`, `Weltall 1.0.3`) |
| Files upstream touched | 91, about 1,400 lines |
| Files the fork touched | 65 |
| Files both touched | 17 |
| Fork commits touching the field rendering files in that overlap | 18 of 42 |
| `git merge upstream/main` | 9 files, 13 hunks in conflict |

The overlap is not spread evenly: it is field rendering and the field's display options, which is
exactly where the fork's see-through code and message-box scaling live.

## What the two releases bring

Windows relevance is the fork's own: it plays on Windows 11 through a Development package.

| Change | In | Matters on Windows |
|---|---|---|
| **Field compass placement** (Options → Display → Compass: 4:3, 16:9, 21:9, Screen, Off; default 16:9) | 1.0.2 | yes, cosmetic and opt-in |
| **Two-button controller shortcuts** (state save/load, slot, message log), plus the same combos on the on-screen controls | 1.0.2, 1.0.3 | yes for the pad; the touch half is Android's |
| **Field fast-forward** (Options → Modding → Balance, off by default; R1/L1 step 1x, 2x, 4x on the field, the world map and in-game scenes) | 1.0.2, 1.0.3 | yes, a new feature |
| **Story battles always begin at Normal** (`FieldProgram.cpp` clears the session's battle speed for a story battle; R1 steps it up again) | 1.0.3 | yes, and it is opinionated: the Battle speed row no longer decides a story fight's opening speed |
| **4:3 display fills the screen** with Aspect 4:3 and Integer scaling off (`AddDisplayFramePasses` draws into a smaller texture, then scales it) | 1.0.3 | only on a 4:3 display, but it rewrites the display path |
| Second controller usable as player one in normal play | 1.0.3 | yes, niche |
| Asset folder falls back to the disc when the folder goes away | 1.0.3 | yes, a real fix |
| Adreno 610 single-sample fallback for MSAA, and its detection fix | 1.0.2, 1.0.3 | no, but it is what reworked the field shader into a `PER_SAMPLE_TEXELS` permutation instead of a runtime `SampleCount` |
| Android C/Z buttons, auxiliary-buttons engine patch, log-export notes | 1.0.2, 1.0.3 | no |

## Where the merge is cheap, and where it is not

Cheap, resolved by keeping both sides: `Config/DefaultGame.ini` (the version), `FieldRenderOptions.h`
(the compass enum beside the fork's message-box and see-through enums), `FieldSceneShaders.h` (the
permutation domain plus the fork's see-through parameters), `PlayerSettings.cpp` (one settings row
each), `MenuSettingsPages.cpp` (one row each in the Display page's row enum), `FieldView.cpp` (one
line each: `Next->Compass` beside `&SeeThroughFade`). `README.md`, `OptionsText.h`,
`Scripts/release.py` and the tests auto-merged.

The three that need thinking:

1. **`Shaders/Private/FieldScene.usf`** (2 hunks). The fork's see-through parameters and its
   coverage loop sit where upstream replaced `SampleCount` with the `PER_SAMPLE_TEXELS` permutation:
   keep the fork's parameters and its `TEXCOORD3`, drop `SampleCount`, and put the fork's
   `SampleCount == MSAA_SAMPLES` test under upstream's `#if`.
2. **`FieldSceneRendering.cpp`** (2 hunks). The same swap in the parameter block and the render
   lambda's capture; the `SeeThrough*` parameters stay, the permutation domain is built from
   `Samples == MsaaSamples` as upstream does.
3. **`FieldScreenFrame.cpp`** (2 hunks). The real one. Upstream rewrote the layer draw:
   `AddLayer()` takes no rectangle, bakes in `ScreenRect`, sets `Frame.End = Cover.LayerIndex` and
   the compass's `Frame.RightShift`, and the cover is simply the last `AddLayer()`. The fork's
   message-box scale splits that same draw into three rectangles around `MessageRange`
   (`ScreenRect`, `WindowRect`, `ScreenRect`). It has to be re-expressed as a rectangle-taking draw on
   the new machinery, and the window rectangle must not pick up the compass's right shift.

## Hazards that do not show up as conflicts

- **The whole-pixel message boxes stop being whole on a 4:3 display.** The fork computes the box's
  scale from `Area.Layout(FieldScreen).Scale()`. In upstream's new 4:3 path the frame is drawn into a
  smaller texture and stretched to the display, so the box is whole inside that texture and then
  scaled by a fraction. The promise in the README needs either a guard or a reworked rule there.
- **The Display page's row order moved.** The enum and the switch cases for the rows were merged
  separately; they have to be checked against `OptionsText` and `MenuSettingsPagesTests`, not merely
  compiled.
- **Version and release.** After the merge the version is `1.0.3-fork.1`: `release.py`'s suffix
  support merged without a conflict, but `check_source`, the `--notes` path and a dry run of
  `--archives-only` are unproven, and `.agents/releases/1.0.1-fork.1.md` says the fork is "the 1.0.1
  release with the changes below", which stops being true.

## Cost

- Conflicts: the six small ones are minutes; the shader and the frame are the work. About 1-2 hours
  with care.
- Verification after: `ctest` in `Tests/` (1,964 pass, 19 listed aborts), the 80 editor rendering
  tests, a Development package (`Scripts/package.py`, 6 minutes) and a walk in the play folder to
  look at see-through scenery and a message box.

## Verdict

Merge now, as a real merge commit, rather than cherry-picking features.

- It is two release commits and 13 hunks; that is the cheapest this will ever be. Upstream is
  shipping weekly, and 18 of the fork's 42 commits sit in the files these two releases touch, so each
  further release makes the same work larger.
- The fork's own rule is that `git fetch upstream && git merge upstream/main` stays boring. It is not
  boring now, and it will not get boring by waiting.
- It brings an opt-in fast-forward, two-button shortcuts, the compass row, and two fixes worth having
  (asset fallback, second controller), none of which the fork's own work provides.
- Cherry-picking the wanted ones would hand-port the same shader and frame changes and leave the next
  merge with a worse conflict.

Two decisions belong to the owner, not the merge: whether to keep upstream's "story battles always
start at Normal" (the fork can restore the setting's say with a one-line change in `FieldProgram.cpp`)
and whether the fork's next release is `1.0.3-fork.1` or a later number.

## Outcome

Merged as commit `e78b27f` on 2026-11-14, with upstream's story-battle rule kept. The nine files were
resolved by keeping both sides, except `FieldScreenFrame.cpp`, which now drives one rectangle-taking
layer draw and lays the message boxes' own scale on top of it.

Verified on the merge: `ctest --preset windows` 1979 of 1998 pass, the 19 aborts are the ones in
`.agents/tests/expected-aborts.txt`; the editor's 85 automation tests all report `Result={Success}`;
`Scripts/package.py Development` compiles the shader permutation and passes its headless check.

Still open: on a 4:3 display with Aspect 4:3 and Integer scaling off, upstream's display pass draws the
frame into a smaller texture and stretches it, so the message boxes' whole-pixel scale is applied
before that stretch and the letters are no longer whole display pixels there. Everywhere else the
promise holds. The merge itself left `ProjectVersion=1.0.1-fork.1`; the next release commit is what
bumps it to `1.0.3-fork.1`, with `.agents/releases/` notes to match.
