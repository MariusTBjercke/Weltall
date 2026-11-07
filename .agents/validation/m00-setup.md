# m00 — Setting up the fork (item 01)

Date: 2026-11-07. Machine: Windows, DSH agent.

## Remotes

```console
$ git remote -v
origin    git@github.com:MariusTBjercke/Weltall.git (fetch)
origin    git@github.com:MariusTBjercke/Weltall.git (push)
upstream  git@github.com:Roman215/Weltall.git (fetch)
upstream  git@github.com:Roman215/Weltall.git (push)

$ git branch -vv
* main 9344d23 [origin/main] Weltall 1.0.1
```

`origin` was the original project before this session; it was renamed to `upstream` and the fork was
added as `origin`. `git push --dry-run origin main` reported `Everything up-to-date` (the fork was
created from the same commit), so write access to the fork is confirmed without pushing anything.

Note for future sessions: Git refused to run at first, with `detected dubious ownership`, because the
checkout belongs to `BUILTIN/Administrators`. Fixed once with:

```console
$ git config --global --add safe.directory C:/Users/micro/source/repos/Weltall
```

## Engine and build

The installed engine is a **Launcher** build (UE 5.8.3, `Engine/Build/InstalledBuild.txt` present), which
is why `BootstrapPackagedGame` could not be compiled: only
`Engine/Binaries/Win64/BootstrapPackagedGame-Win64-Shipping.exe` exists, not `Engine/Source/Programs/BootstrapPackagedGame`.
`Scripts/package.py` now skips that step when the sources are absent.

```console
$ python Scripts/package.py Development
Exporting 9344d23 into out\package\Weltall
Development: building for Windows (log: out\builds\Development.log)
BUILD SUCCESSFUL
Development: built in 6 min: out\builds\Development\Windows\Weltall.exe
Development: ran for 60 s (log: out\builds\Development-check.log)
```

The check's log, which is the part that proves it actually came up:

```
LogWeltall: Found 2 of the game's discs in C:/Users/micro/source/repos/Weltall/out/builds/Development/Windows/Games
LogWeltall: Story documents read: 4
LogWeltall: Now running the Movie program
LogWeltall: Now running the Field program
```

Package: 1,656 MB in `out/builds/Development/Windows`, `Revision.txt` = `9344d23`.

## Game data and the play folder

Discs were copied from the released build into the git-ignored `Games/` (same volume as `out/`, so
`link_games()` hard-links them into the package):

```console
$ fsutil hardlink list "out\builds\Development\Windows\Games\Xenogears (USA) (Disc 1).bin"
\Users\micro\source\repos\Weltall\Games\Xenogears (USA) (Disc 1).bin
\Users\micro\source\repos\Weltall\out\builds\Development\Windows\Games\Xenogears (USA) (Disc 1).bin
```

```console
$ .\Scripts\build.ps1 -LinkOnly
play folder: D:\Games\Weltall -> C:\Users\micro\source\repos\Weltall\out\builds\Development\Windows
```

```console
$ (Get-Item D:\Games\Weltall -Force) | Select-Object LinkType,Target
Junction  {C:\Users\micro\source\repos\Weltall\out\builds\Development\Windows}
```

The junction helper was tested in isolation before use: `cmd /c rmdir` on the junction left the target
and its contents intact, and a real folder (not a reparse point) is refused rather than deleted.

`D:\Games\Weltall-1.0.1-Windows` still holds the untouched release (Assets, Docs, Engine, Games, Mods,
Weltall, Weltall.exe).

## Core tests

Built in ~25 minutes and run with the discs present:

```console
$ cd Tests; cmake --preset windows; cmake --build --preset windows; ctest --preset windows
...
99% tests passed, 19 tests failed out of 1983
Total Test time (real) =  85.35 sec
```

The 19 are not failures of the code under test. Each reaches `REQUIRE_DISC` with a release whose disc
is optional (the English undub, a fan translation, Perfect Works Build). `REQUIRE_DISC` uses Catch2's
`SKIP`, this project compiles with exceptions off, and Catch2 therefore terminates the process:

```console
$ out\tests\windows\Release\WeltallCoreTests.exe "Supported undub discs power on and enter the field with English text"
VoiceLanguageContentTests.cpp(51): SKIPPED:
explicitly with message:
  disc not found in Games/: Release

Catch will terminate because it needed to throw an exception.
The message was: Explicitly skipping tests during runtime requires exceptions
=== exit: C0000409 ===
```

The same binary run against a disc that *is* present returns `00000000` and passes. The exact 19 are
snapshotted in `.agents/tests/expected-aborts.txt`, so a failure outside that set is a real regression.

## Not proven

- The game was started **headless** (`-nullrhi -nosound -unattended`) by the check. Nothing was played
  interactively from `D:\Games\Weltall` in this session.
- Only the `Development` configuration was built. `Shipping`, Linux and Android are unexercised.
- The 19 optional-data suites above have never run here, so undub, translation and Perfect Works
  behaviour is entirely unverified on this machine.
