# Requirements, constraints and feature plan

Written 2026-10-27 from the user's own words over the project. Order of precedence: the user's latest message wins.

## 1. Product principle (2026-10-27)
"I just want the game's base to be 1:1 but have an option to change to an expanded version later." So:
- The default build and the "1:1 original" preset reproduce the PS2 game exactly (behaviour, visuals, audio, timing, quirks).
- Everything beyond that (new modes, imported content, higher resolutions, texture packs, extra soundtracks, larger memory, settings menu changes) is opt-in through the launcher or the in-game settings menu, from one shared config layer, and never alters the 1:1 behaviour when off.
- Later: verify 1:1 against the original code (coverage + PCSX2 state/GS comparison), including a way to prove it.

## 2. Standing constraints
- Never upload game data (live_ram.bin, live_vram.bin, ISO/AFS/PAC contents, ELF, generated game code, decoded overlay images). Run `python tools\check_clean.py` before publishing.
- No injected or scripted input in shipped builds; no run-time limits or timers in shipped builds. Live-console `press` is for testing only.
- Report the completion percentage after each result/rebuild (format in `status/completion-tracking.md`).
- Produce `docs\FINDINGS.md` when done (research for future projects).
- Close old PCSX2 instances before starting a new one; prefer debugging the running game directly.
- Project rules: never clean the build; never edit generated runner `sub_*.cpp` (except through deliberate regeneration); header edits allowed when needed (OK given 2026-10-27); no careless wide greps over the runner/overrides dirs; memory card first, then Options -> Unlock All; critical issues before minor; short polls only.
- Memory limits: raise only for performance improvements or when needed for correctness; leave headroom for future modding (optional large-memory mode, off by default).

## 3. Correctness/UX requirements (in force now)
1. Game must not pause when the window is dragged or resized.
2. Steady 60 FPS including loading screens and memory-card loads.
3. Loading screen without junk rows; one Start press on Press START leads to the idle demo; no doubled music.
4. XInput gamepad support; native 60 fps; widescreen/ultrawide; 4:3 with correct scaling (not stretched).
5. Memory-card round trip (create/save/load).
6. Options -> Unlock All.
7. Audio working.
8. Performance.
9. All regions eventually (USA/Japan/Korea/Italy folders exist under `assets\`).
10. IPU port attribution (PCSX2, GPLv3).

## 4. Approved feature wishlist (not started)
- Launcher with settings and a "1:1 original" preset (one config layer, also exposed through a baked-in in-game settings menu for players who skip the launcher).
- Soundtrack manager: import soundtracks from other SVR games, dropdown to choose, import own songs, on-screen "artist - song name" overlay like the base game.
- Content importer: character models, Titantrons, move-sets from earlier SVR games most compatible with SVR06 ("the most definitive edition").
- Mod folder with hot reload; quality options (resolution scale, widescreen, filtering, 60 fps lock); controller remap and quick resume; roster/attire editor; custom match rules; challenge mode.
- Optional PCSX2 memory-card format: launcher asks the save format; if PCSX2 is chosen the player selects a card file; never reliant on PCSX2; backup before every write.
- Texture packs up to 8K.
- Platforms: Linux, Android, Steam Deck, lower-end PCs (needs the planned Vulkan GPU backend; the software GS stays as the reference).
- New modes/ideas: the assistant brainstorms and the user approves from a list as ideas arise.

## 5. Architecture implications for new code
- One configuration layer read by launcher, in-game menu and runtime; the 1:1 preset is a fixed profile that overrides every other option.
- Renderer abstraction between the GS front end and the backend so a Vulkan backend can be added next to the software one.
- Audio abstraction that can swap the music source per game and per user file while the default selects the disc track.
- Save abstraction with two formats (native host folder, PCSX2 `.ps2` card) behind the same HLE `sceMc*` layer.
- Content importers work on user-supplied files only (no game data shipped).
