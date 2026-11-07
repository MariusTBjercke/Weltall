# Definition of done

Observable outcomes, each with the command or check that proves it. The fork is done with an item when
these hold for it, and done with the project when the owner's list is empty and plays well.

## Setup (item 01)

- `python Scripts/package.py Development` exits 0 and ends with `Development: ran for ... s`.
  Proved by `out/builds/Development.log` and `out/builds/Development-check.log`.
- `D:\Games\Weltall\Weltall.exe` starts the game and finds both discs. Proved by the check's log line
  `Found 2 of the game's discs` and by starting it by hand.
- `git remote -v` shows `origin` = the fork and `upstream` = the original.
  Proved by `git remote -v; git branch -vv` (`main` tracks `origin/main`).
- Tests: `cmake --preset windows && cmake --build --preset windows && ctest --preset windows` in
  `Tests/` passes, with the disc-dependent suites running (not skipped).

## Each tweak item

- The change is described in `README.md` in player language, next to the feature it belongs to.
- If it is optional, it has a switch in Options, defaults to something sensible for the owner, and
  switching back gives the previous behaviour.
- `.\Scripts\build.ps1` succeeds and its check passes: the package comes up and finds the discs.
- The owner has played the affected part of the game from `D:\Games\Weltall` and accepted it.
- For `WeltallCore` logic: the relevant CMake suite still passes.
- One commit, named for what changed for the player, in upstream's commit style.

## Never acceptable

- Game data, extracted assets or logs committed.
- A build claimed to work because it compiled, without the check's log or a play session behind it.
- Silent divergence from upstream: a fork change that cannot be told apart from an upstream patch when
  merging.
