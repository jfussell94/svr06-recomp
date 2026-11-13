# Changelog
Summary of releases. The full engineering log (CL-nnnn / ISS-nnn) is in `CHANGELOG-detailed.md`.

## Unreleased
- Runtime fixes (build OK, in-game verification pending): guest `memcpy` no longer uses undefined overlapping `memcpy` (now `memmove`); GS no longer writes depth when TEST.ZTE=0; IOP defers DMA-complete interrupts while the guest holds a `CpuSuspendIntr` section (capped at 256 deferrals).
- `ps2xIOP` (IOP emulator) sources are now part of the repository.
- Whole-game reverse-engineering tools and name tables added under `tools/` (see README and `docs/CHANGELOG-2026-11-12.md`).
- Known open: ~60 s pause before an entrance cutscene starts (between the event overlay's first and second resource load).
- Launcher: Play-tab version selector (PAL / USA), Games page (all PS2/PSP versions), Cheats tab (framework), controller button icon sets.
- USA (SLUS-21286): first build pipeline (`scripts/USA_BUILD.cmd`) with PAL->USA address mapping. Boots; overlay banks pending.
- Launcher: GitHub token save/test status indicator, About & licenses page, per-version cheats / play settings / logs, version status stages (planned, scaffolded, bootable, menus, ingame, playable, release) instead of a single "playable" flag, blank memory card button, typed-text colour fix in dark mode.
- Versions can run side by side: each region runs as its own executable (`SVR06_PAL.exe`, `SVR06_USA.exe`); launching one only closes the same region; logs are kept per version.
- USA (SLUS-21286): PAL game fixes are now ported through `tools/usa_port_overrides.py` (hook addresses re-mapped to USA), PAL-only icon set / texture pack are not applied to USA, USA movie paths fixed. Status: bootable - reaches Arena Select, the arena picture/backgrounds are not loaded yet (ISS-161, under investigation).
