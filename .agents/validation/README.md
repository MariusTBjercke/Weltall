# Validation

Evidence that work was actually verified, one file per milestone or per substantial item: the command
that was run, the relevant part of its output, what it was compared against, and what is still off.

This exists because a build that compiles and a check that passes are not the same as the game being
right, and because "it worked" without the log line is how regressions get shipped.

Rules:

- Paste the command and the decisive lines of its output, not a summary of them.
- Reference the artifact: `out/builds/Development-check.log`, a screenshot, a save file path. Those
  artifacts are git-ignored and may disappear, so quote what matters.
- State what is *not* proven. A start-up check proves the game boots, finds its discs and reads its
  story documents — it proves nothing about a battle animation.
- Never copy game data or extracted assets in here; quote only the lines needed.
