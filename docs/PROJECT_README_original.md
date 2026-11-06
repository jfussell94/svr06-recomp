# svr06-recomp
Prepared workspace for a static recompilation of WWE SmackDown! vs. Raw 2006 (PAL).
- `tools/` analyzers (Python 3, no deps) | `analysis/` generated reports | `config/` recompiler config + HLE plan
- `scripts/setup.ps1` installs/builds the toolchain | `docs/` roadmap + Ghidra steps | `out/` generated C++ | `mods/` custom content
Re-run analysis: `scripts\run_analysis.cmd`. Game files are in `..\assets` (not modified).

## Legal / what is NOT in this repo
No game files, no ELF, and no code generated from the game are included. You must supply your own legally obtained disc dump
(extract it to `..\assets`). Generated C++ (`out/`), analysis output, and build folders are git-ignored and produced locally
by `scripts\run_analysis.cmd` and `scripts\BUILD_AND_RUN.cmd`. Run `python tools\check_clean.py` before publishing.
