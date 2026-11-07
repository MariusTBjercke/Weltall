# Context

## What this repository is

A personal fork of **Weltall** (`Roman215/Weltall`) — a native Unreal Engine 5 port of *Xenogears*
(PlayStation, 1998). The port ships no game data; it reads the player's own disc images and reimplements
the original game's code on top of an emulated PlayStation.

- Fork: `git@github.com:MariusTBjercke/Weltall.git` — `origin`, where work is pushed.
- Original: `git@github.com:Roman215/Weltall.git` — `upstream`, read-only here.

## Goal of the fork

Change the game's behaviour and its options to fit one player's taste: features that are missing, and
existing behaviour that should feel different. The owner plays the released 1.0.1 build, notes what is
missing, and that list becomes the work items.

**The list is not written down yet.** It is the first thing to fill in — see `progress.json`, item 02.
Until then, do not invent work items.

## Hard constraints

- **No game data in the repository.** Discs, extracted maps/textures/audio, saves and logs stay out;
  `.gitignore` covers `Games/`, `Assets/`, `Mods/`, `out/`.
- **Upstream stays mergeable.** `git fetch upstream && git merge upstream/main` must stay boring.
  Fork-only changes belong in the files that already own that concern, kept small and separable.
- **Every player-visible change is switchable or documented.** The project's whole premise is that
  options let the player choose modern behaviour or the original; a fork change follows the same rule
  and is described in `README.md`.
- **Gameplay semantics come from the original.** The port is checked against the original code with
  oracle tests. Do not change emulation behaviour to make a tweak easier; change the option layer.

## Source of truth

| Question | Where |
|---|---|
| What does the game/player see today? | `README.md` — every feature and option, in player language |
| How do options get defined and drawn? | `Source/WeltallCore/Public/Menu/OptionsText.h`, `MenuOptions.h`, `MenuSettingsPages.h` |
| How is the game packaged and released? | `Scripts/package.py`, `Scripts/release.py` |
| How does building and testing work here? | `AGENTS.md` |
| Commit style and language rules | `.agents/conventions.md` |

## Vocabulary

- **Item** — one tweak from the owner's list, small enough to land as one commit (or a short series).
- **Play folder** — `D:\Games\Weltall`, a junction to the newest build, where the owner plays.
- **Reference build** — `D:\Games\Weltall-1.0.1-Windows`, the untouched released 1.0.1 build.
- **The check** — `Scripts/package.py`'s headless start of the built game, which reads its log and
  asserts it came up, read the story documents and found the discs.
