# svr06-recomp

Native Windows port of **WWE SmackDown! vs. Raw 2006** through static recompilation of the PS2 executable ([PS2Recomp](https://github.com/ran-j/PS2Recomp)) and a Vulkan renderer ([paraLLEl-GS](https://github.com/themaister/parallel-gs)).

| Region | Serial | Status |
|---|---|---|
| Europe / Australia (PAL) | SLES-53676 | In-game - matches run, known crashes and glitches |
| North America (NTSC-U) | SLUS-21286 | Bootable - boots to the logos, black screen afterwards while the PAL fixes are ported |
| Italy (PAL) | SLES-53677 | Planned |
| Japan (NTSC-J) | SLPM-66268 | Planned |
| Korea (NTSC-K) | SLKA-25318 | Planned |

Stages: Planned, Recompiled, Bootable, Menus, In-game, Playable (Playable is reserved for the 1.0 release).

## No game content in this repository
Only original tooling lives here: recompiler configuration (addresses/options), runtime hooks, our changes to the PS2Recomp runtime, the launcher, scripts and documentation.
There is no game executable, disc data, dump, screenshot or code generated from the game. **You must supply your own legally obtained disc dump.**
Run `python tools/check_clean.py` before publishing anything.

## Quick start (Windows)
1. Clone this repo, then follow [BUILDING.md](BUILDING.md).
2. Put your disc files under `assets/` (folder per region is fine).
3. Run `scripts\LAUNCHER.cmd` (or the packaged `SvR06Launcher.exe`). Pick the version on the Play tab, then **Rebuild + play**.

## Layout
| Path | Contents |
|---|---|
| `regions/<id>/` | One folder per game release: `region.json`, `recomp.toml`. No forks needed - add a folder. |
| `PS2Recomp/` | Upstream PS2Recomp plus our runtime changes (Vulkan GS bridge, IOP/SPU2 work, hooks) |
| `overrides/` | Hand-written runtime hooks |
| `tools/launcher/` | The launcher (Python backend + web UI; builds to `SvR06Launcher.exe`) |
| `tools/` | Analysis, address-mapping (`usa_map.py`) and overlay tools |
| `scripts/` | Build/run scripts, `USA_BUILD.cmd`, `FAST_REBUILD_AND_RUN.cmd` |
| `release/` | Release packaging (`package_release.py`, `regions.json`) |
| `docs/` | Findings, roadmap, handoff notes. `CHANGELOG.md` / `CHANGELOG-detailed.md` |

## Reporting bugs
Use the launcher's **Report issue** page (it opens a GitHub issue with your version and log excerpt attached and later asks you to confirm the fix), or open an issue here with the bug template.

## Releases
Tagged releases ship the **launcher app** and tooling per platform. Binaries containing code translated from the game are not distributed. See [release/README.md](release/README.md).

## License
Upstream PS2Recomp is MIT licensed. A license for the original code in this repository has not been chosen yet - add a `LICENSE` file before accepting contributions.
