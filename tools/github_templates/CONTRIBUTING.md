# Contributing
- Never commit game files, dumps, screenshots of game content, or code generated from the game. `python tools/check_clean.py` must pass.
- One region = one folder under `regions/`. Do not fork per region.
- Document every change in `CHANGELOG-detailed.md` (CL-nnnn / ISS-nnn).
- Tickets: commit messages that fix a ticket say `Refs #12` (not `Fixes #12`). The workflow labels it `fixed-awaiting-confirmation`; the reporter closes it from the launcher once verified.
