# Changelog
Summary of releases. The full engineering log (CL-nnnn / ISS-nnn) is in `CHANGELOG-detailed.md`.

## Unreleased

### 2026-11-25 (CL-0396 - CL-0409) - v0.9.0-alpha.5
- **USA builds from the USA disc alone:** no PAL disc, no emulator dump. Shipped USA config and hooks, overlay banks decoded from `BIN.PAC` (`tools/usa_ovl_all.py`), recompiler built on first use, paraLLEl-GS fetched at a pinned commit with our patches (`patches/`).
- **Launcher:** link a disc image (`.iso`) as well as an extracted folder (unpacked once into `mods/discs/`); build progress now shows during silent compile/link phases; works in a release checkout (paths inside the repo or next to it); Play on USA no longer falls back to the PAL boot file.
- **Custom folders:** `Custom Music`, `Custom Titantrons` and `Custom Entrances` now live in the repo root (older ones in the build folder are moved there automatically).
- **Fixes:** fullscreen hides the Win32 menu bar; entrance/menu audio is restored when a save state is loaded; missing `Svr06Ui` type, recompiler `main.cpp` and `xxhash` that stopped fresh checkouts from compiling.
- **Releases:** assets are uploaded to a draft that the workflow then publishes (the repository uses immutable releases); hyphenated tags are pre-releases.
- Known: the clean-checkout USA build was verified through to a booting game (frames render); full play-through on a tester build is still being checked.

### 2026-11-16 to 2026-11-25 (CL-0272 - CL-0395) - v0.9.0-beta
- **Playable end to end on USA (SLUS-21286):** SmackDown Season and RAW Season play through, including the final cutscenes; the stuck/flickering Season cutscene scene, the black arena after a Season match and several load hangs are fixed.
- **Pacing:** vsync/frame pacing fixes (no more slow motion without a manager), GS queue tail merge for menu lag.
- **Roster/content:** roster database, import of SvR05/HCTP wrestlers, cut-wrestler restore, extra entrance music and movies, movesets, custom entrance database (launcher import tools).
- **Debugging:** Win32 menu bar and debugger window (breakpoints, registers, disassembly, memory find), GPU timing log.
- **Save states (new):** in-session save/load states (4 slots, F6/F7) that capture RAM, the EE kernel, GS VRAM, the IPU video decoder and the host audio streams.
- Known open: Rumble/crowd slowdown (profiling), voice drift at camera cuts, a post-match stall in the WrestleMania 21 final cutscene, PAL build not re-verified.

### 2026-11-13 to 2026-11-15 (CL-0217 - CL-0271)
- **Speed.** Matches and entrances run at full speed; a crowded Royal Rumble went from 32-36 to 46-49 fps (PAL, Ryzen 5 5600X):
  native VU1 programs generated locally from your own disc (never published; without them the runtime uses the interpreter),
  async VIF1/GIF worker with batched GS submission, faster VIF UNPACK, cheaper VU1 result/flag handling, AVX2 code generation
  (the build now needs a CPU with AVX2), runtime trace logs off.
- **Title screen** rebuilt to hardware behaviour: the title movie plays under the PRESS START pill and text, the demo starts
  after the idle timeout. Boot movies restored.
- **Season mode:** post-match cutscene dialogue plays (voice banks SSND1/SSND2) and the scene moves on.
- **Display:** window size presets 480p to 4K, borderless or exclusive fullscreen (F11), integer scaling, picture adjustments;
  shared between the launcher and the in-game F1 menu.
- **Launcher:** new look ("ring apron": SmackDown blue / Raw red, steel plates, bundled Archivo font), fully borderless window
  with its own title bar and resize edges, pages grouped into Play / Library / Video / Audio / Controls / Gameplay / Mods /
  Support, Ctrl+K search, video presets, project icon.
- **USA (SLUS-21286)** now builds from exactly the same runtime as PAL, with the overlay hooks (title screen, menus, Season)
  mapped to the USA disc; runs at 60 Hz. Texture pack, icon sets and cheats remain PAL-only.
- Known open: cloth/clothing physics glitches, a Season cutscene freeze when Bischoff comes out, brief corruption around the
  title -> PRESS START hand-off.

- Runtime fixes (build OK, in-game verification pending): guest `memcpy` no longer uses undefined overlapping `memcpy` (now `memmove`); GS no longer writes depth when TEST.ZTE=0; IOP defers DMA-complete interrupts while the guest holds a `CpuSuspendIntr` section (capped at 256 deferrals).
- `ps2xIOP` (IOP emulator) sources are now part of the repository.
- Whole-game reverse-engineering tools and name tables added under `tools/` (see README and `docs/CHANGELOG-2026-11-12.md`).
- Known open: ~60 s pause before an entrance cutscene starts (between the event overlay's first and second resource load).
- Launcher: Play-tab version selector (PAL / USA), Games page (all PS2/PSP versions), Cheats tab (framework), controller button icon sets.
- USA (SLUS-21286): first build pipeline (`scripts/USA_BUILD.cmd`) with PAL->USA address mapping. Boots; overlay banks pending.
- Launcher: GitHub token save/test status indicator, About & licenses page, per-version cheats / play settings / logs, version status stages (planned, scaffolded, bootable, menus, ingame, playable, release) instead of a single "playable" flag, blank memory card button, typed-text colour fix in dark mode.
- Versions can run side by side: each region runs as its own executable (`SVR06_PAL.exe`, `SVR06_USA.exe`); launching one only closes the same region; logs are kept per version.
- USA (SLUS-21286): PAL game fixes are now ported through `tools/usa_port_overrides.py` (hook addresses re-mapped to USA), PAL-only icon set / texture pack are not applied to USA, USA movie paths fixed. Status: bootable - reaches Arena Select, the arena picture/backgrounds are not loaded yet (ISS-161, under investigation).
