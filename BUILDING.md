# Building

Windows 10/11, PowerShell. Needed: Git, CMake, Ninja, Python 3.10+, Visual Studio 2022+ with the LLVM/clang-cl component (`-msse4.1 -mssse3` are required), FFmpeg dev libraries as used by the PS2Recomp runtime.

Workspace layout the scripts expect (sibling folders):

    <workspace>\svr06-recomp\   this repo
    <workspace>\PS2Recomp\      upstream clone + this repo's PS2Recomp changes (scripts\setup.ps1 prepares it)
    <workspace>\assets\         YOUR game files, e.g. assets\SLES_536.76 or assets\<disc folder>\SLUS_212.86

## PAL (SLES-53676)
1. `scripts\setup.ps1` - clones PS2Recomp, applies our runtime sources, builds the recompiler.
2. `ps2_recomp regions\pal\recomp.toml` (run from `regions\pal`) - writes `out\generated_r4\` (git-ignored).
3. `scripts\link_runtime.ps1` - copies generated code + `overrides\` into the runtime and builds `ps2EntryRunner`.
4. `scripts\run.cmd`, or use the launcher (**Rebuild + play**).

## NTSC-U (SLUS-21286) - first build
`scripts\USA_BUILD.cmd` does everything: address map, recompile, a separate runtime tree (`..\PS2Recomp_usa`) and the build. PAL outputs are never touched.
Overlay banks for USA are still to be generated, so it boots only partway.

## Overlays
Runtime-loaded code (11 overlays in `BIN\BIN.PAC`) is recompiled from a RAM dump with `tools\ovl_recomp.py`; see `docs\HANDOFF.md`.

## Launcher app
`tools\launcher\build_launcher.ps1` builds `dist\SvR06Launcher.exe` with PyInstaller. CI builds it on every tag (see `.github\workflows\release.yml`).
