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

## NTSC-U (SLUS-21286) - builds from the USA disc alone (no PAL disc)
Needs: Windows 10/11, Visual Studio with the LLVM/clang-cl component (the scripts look in `C:\Program Files\Microsoft Visual Studio\18\Community`), Git on PATH, CMake, Ninja, Python 3.10+, and internet access for the first build (FFmpeg, raylib, paraLLEl-GS).

1. Put the disc files in `assets\<any folder>\` (`SLUS_212.86` and `BIN\BIN.PAC` must be inside), **or** link your `.iso` on the launcher's Library page - it is unpacked once into `mods\discs\ps2_usa`.
2. Run `scripts\USA_BUILD.cmd` (or **Rebuild + play** in the launcher with the USA version selected). It does, in order:
   - uses the shipped USA config (`regions\usa\recomp.toml`) and USA hooks (`overrides\usa`) - no PAL->USA address mapping;
   - builds the recompiler (first time) and recompiles `SLUS_212.86`;
   - decodes the 10 overlay banks (menus, title, Season, ...) from `BIN.PAC` with `tools\usa_ovl_all.py` - the RAM image it scans is made from `SLUS_212.86` itself (`tools\usa_make_dump.py`), no emulator dump needed;
   - fetches paraLLEl-GS at its pinned commit and lays `patches\parallel-gs` (our LGPL changes) over it, plus `patches\xxhash`;
   - builds `ps2EntryRunner` into `PS2Recomp_usa\build_svr06_usa`.
3. The first build takes roughly one to two hours (the final link alone is 5 to 8 minutes and prints nothing). Log: `analysis\usa_build_log.txt`. PAL outputs are never touched.

## Overlays
Runtime-loaded code (11 overlays in `BIN\BIN.PAC`) is recompiled with `tools\ovl_recomp.py`; see `docs\HANDOFF.md`. PAL uses a RAM dump of the running game; USA is built from the disc alone by `tools\usa_ovl_all.py`.

## Launcher app
`tools\launcher\build_launcher.ps1` builds `dist\SvR06Launcher.exe` with PyInstaller. CI builds it on every tag (see `.github\workflows\release.yml`).
