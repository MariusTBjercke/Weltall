# Conventions

Rules this fork follows so that changes stay reviewable against upstream and usable by agents.

## Language

- **Code, comments, documentation and commit messages: English.** The upstream project is English and
  patches to it have to read the same way.
- **Chat with the maintainer: Norwegian** is fine and expected; that never reaches the repository.
- Player-facing text that ships in the game follows the existing tone in `Source/WeltallCore` and the
  README: plain, concrete, no exclamation marks, no marketing.

## Commits

Match what upstream already does — plain, descriptive subjects in the imperative, capitalised, no
type prefixes. Upstream has **no** Conventional Commits (`feat:`, `fix:`) in its history; don't start.

Good, in upstream's voice:

```
Add a Button layout row: Circle confirms, or the Japanese release's layout
Start a render's button glyphs from the controls its settings give
Use plainer wording in docs and release notes
```

- One logical change per commit; the message says what it changes for the player, not which files moved.
- A body only when the *why* is not obvious from the subject. Two of the last fifty upstream commits
  have one, so keep it rare.
- Release commits are the exception and are named after the version: `Weltall 1.0.2`.
- Never mention the agent, the tool, or the session in a commit message.

## Branches

- `main` — the fork's working branch, always buildable. It tracks `origin/main`.
- Working branches are optional for this fork; a short descriptive name is enough
  (`faster-text`, `camera-zoom`). No ticket-number prefixes; there is no ticket system.
- `upstream/main` is read-only here: bring changes in with a merge, never a rebase that rewrites
  commits shared with upstream.

## Applying changes

- Keep fork-only additions small and separable from upstream code, so that
  `git fetch upstream && git merge upstream/main` stays boring. A new option belongs in the files that
  already own that concern; prefer extending an existing table or enum over adding a parallel path.
- Every player-visible change needs its switch (or its default) described in `README.md` and, when it
  has one, in the Options page text.
- Nothing derived from game data may be committed: no maps, textures, audio, subtitle rips, saves,
  logs. `.gitignore` covers `Games/`, `Assets/`, `Mods/`, `out/`.
- Version bumps happen in exactly one place, `Config/DefaultGame.ini` (`ProjectVersion=`), together
  with the `Weltall <version>` commit.

## Verification before claiming a change works

1. Commit first — `Scripts/package.py` builds `git archive HEAD`, so uncommitted work is not built.
2. Build: `.\Scripts\build.ps1`. It compiles, cooks, stages and starts the game headless, and fails
   loudly if the package does not come up.
3. Play the affected part of the game from `D:\Games\Weltall` (the fixed play folder). Automated checks
   prove the game starts; only playing proves the feature.
4. For `WeltallCore` logic, also run the CMake suites from `Tests/` (see `AGENTS.md`).

State what was actually run in the commit or the handoff, not what "should" hold.
