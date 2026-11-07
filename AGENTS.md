# AGENTS.md

Guidance for AI agents and future maintainers. Read this first; it should be enough to build,
run and test the game without rediscovering the setup.

This checkout is a **personal fork** of the original Weltall port:

- `origin` — `git@github.com:MariusTBjercke/Weltall.git` (this fork; push here)
- `upstream` — `git@github.com:Roman215/Weltall.git` (the original project; pull ideas and fixes from it)

`main` tracks `origin/main`. See `.agents/plans/00-context.md` for what the fork is for.

> Deviation from the usual layout: internal/agent documentation lives in `.agents/`, **not** `docs/`.
> `Docs/` (capital D) is upstream's player-facing documentation and is copied into every packaged
> build. On Windows `docs/` and `Docs/` are the same directory, so a lowercase `docs/` is not possible
> here and must not be introduced.

## At a glance

Weltall is a native Unreal Engine 5 port of *Xenogears* (PlayStation, 1998). It ships no game data:
the player supplies their own disc images in `Games/`, and the executable reimplements the original
game's code against them. The project is ~230k lines of C++ across four trees:

| Path | Role |
|---|---|
| `Source/WeltallCore` | The whole game: PSX hardware emulation, formats, battle, field, world map, menus, story. **No Unreal dependencies**, so it builds and tests on its own. ~208k lines. |
| `Source/Weltall` | The Unreal layer: rendering, audio, input, screens, shell subsystems. ~19k lines. |
| `Source/WeltallEditor` | Editor-only commandlets and the rendering tests. ~9k lines. |
| `Tests/` | Catch2 suites for `WeltallCore` plus the offline tools (`WeltallMapText`, `WeltallSortingAudit`, `WeltallStorySweep`). ~111k lines. |

Supporting trees: `Config/` (engine/project settings), `Content/Story` and `Content/Examples` (the
port's own story documents, staged as loose text into packages), `Shaders/`, `Docs/` (player docs),
`Scripts/` (packaging and releases), `ThirdParty/Catch2`, `Build/` (packaging icons only).

## Build

The engine is an **installed (Launcher) build**, not a source build: UE 5.8.3 at
`C:\Program Files\Epic Games\UE_5.8`, with `Engine/Build/InstalledBuild.txt` present. `UE_ROOT` is set
in the git-ignored `local.env` at the repository root; `local.env.example` lists every setting it takes
and is the file to copy on a new machine.

```powershell
python Scripts/package.py Development        # compile, cook, stage, then run a headless check
.\Scripts\build.ps1                          # same, then refresh the fixed play folder (see below)
.\Scripts\build.ps1 -Configuration Shipping  # a release-grade build
```

`Scripts/package.py` does, in order:

1. `git archive HEAD` → `out/package/Weltall` (a project tree of its own, so cooks never lock your
   editor). **It builds the committed revision, so uncommitted edits are silently left out.**
2. `UnrealBuildTool WeltallEditor Win64 Development` and `UnrealBuildTool Weltall Win64 <config>`.
3. `RunUAT BuildCookRun ... -skipbuild -cook -stage -pak -package -archive`.
4. Moves the staged tree into `out/builds/<Configuration>/Windows` and hard-links `Games/` beside it.
5. `--check-only`-style verification: starts the package with `-nullrhi -nosound -unattended` for
   `--check-seconds` (default 60) and asserts its log says the game is running, read every story
   document and found the discs.

The full log of a build is `out/builds/<Configuration>.log`; the check's log is
`out/builds/<Configuration>-check.log`. `package.py` prints nothing while it compiles, so watch the log
file for progress. A cold `Development` build took **6 minutes** on this machine (~790 compile units in
the editor target, then the game target, the cook and the 60-second check).

### One local change to `package.py`

`build()` used to always compile `BootstrapPackagedGame` — the bootstrapper that becomes a package's
top-level `.exe`. A Launcher engine ships only the compiled binary
(`Engine/Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe`) and not
`Engine/Source/Programs/BootstrapPackagedGame`, so that step failed with "target not found". The
command is now guarded by the presence of the sources, which keeps source builds working. If you ever
move to a source-built engine, nothing needs to change.

## Test

`WeltallCore` has no Unreal dependency and builds with plain CMake 3.25+ and a C++20 compiler. The
targets compile with the same constraints as Unreal's build: **no exceptions, no RTTI, warnings are
errors** (`/W4 /WX` on MSVC).

```powershell
cd Tests
cmake --preset windows          # generator: Visual Studio 18 2026, output ../out/tests/windows
cmake --build --preset windows
ctest --preset windows
```

Tests read disc images from `WELTALL_GAMES_DIR` (`<repo>/Games`, set by `Tests/CMakeLists.txt`) and
**report a skip when a disc is missing** — a green run without the discs has not tested the game logic.
Oracle tests compare the port against stored digests of the original code running in an emulated CPU.

`WeltallMapText <map id>` prints a disc map in the text format that story documents start from. Its
output contains original game scripts and text: inspect it, never commit it.

## The play folder

`D:\Games\Weltall-1.0.1-Windows` is the **untouched released 1.0.1 build** — treat it as read-only
reference, including its `Games/` discs, which were copied into this checkout's `Games/`.

Builds go to this checkout, and `D:\Games\Weltall` is a **directory junction** to
`out\builds\<Configuration>\Windows`, created and refreshed by `Scripts\build.ps1`. Running the game
from the fixed path and copying the package around therefore stay separate concerns, and no disc
images are duplicated. If the junction is missing, recreate it with:

```powershell
.\Scripts\build.ps1 -LinkOnly
```

## Verified facts

Re-verify before contradicting any of these; each has a one-line check.

| Fact | Check |
|---|---|
| Engine is UE 5.8.3, installed build, patch level 58210709 | `Get-Content "C:\Program Files\Epic Games\UE_5.8\Engine\Build\Build.version"` — date 2026-11-07 |
| `BootstrapPackagedGame` sources are absent from the installed engine | `Test-Path "C:\Program Files\Epic Games\UE_5.8\Engine\Source\Programs\BootstrapPackagedGame"` is `False` |
| `WeltallEditor` needs 790 compile units on a cold build | count `[n/790]` lines in `out/builds/Development.log` |
| A cold `Development` build took 6 min and produced a 1.66 GB package | the `built in N min` line in the build's output, 2026-11-07 |
| Version lives in exactly one place | `git grep -n "ProjectVersion=" -- Config` → `Config/DefaultGame.ini` |
| Settings and saves live outside the repo | `%LOCALAPPDATA%\Weltall` (`Saved/`), crashes in `Saved/Crashes` |
| Discs are git-ignored and never packaged | `.gitignore` has `/Games/`; `archive()` skips `Games`, `Assets`, `Mods` |
| Packaging links `Games/` with hard links, so source and build must share a drive | `link_games()` in `Scripts/package.py` |

## Pitfalls

- **Uncommitted work is not built.** `package.py` exports `git archive HEAD`. Commit (or use
  `--revision <sha>`) before packaging, or you will test old code and not notice.
- **`Games/` and `out/` must stay on the same drive.** `link_games()` hard-links the discs; across
  drives it only prints a hint and the package starts without discs, which fails the check.
- **A running game blocks the rebuild.** The old package is renamed aside first; if the game holds
  it, `package.py` reports it and leaves the new build in `out/builds/<Configuration>.new`.
- Don't put internal notes under `Docs/`: that tree is shipped to players, and `docs/` collides with
  it on Windows.
- Don't commit anything derived from game data (map text, extracted textures, audio, saves).

## Reading order for a fresh session

1. This file.
2. `.agents/plans/00-context.md` — what this fork is trying to change.
3. `.agents/plans/progress.json` — what is done and what is next.
4. `README.md` — the player-facing view of every feature and option (it doubles as the feature map).
5. `.agents/conventions.md` — commit style and language rules before writing anything.

## Conventions

See `.agents/conventions.md`. In short: English in code, comments, docs and commit messages; plain
descriptive commit subjects that match upstream's existing style (no Conventional Commits prefixes);
one logical change per commit.
