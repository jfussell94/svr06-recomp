# SVR-06 recomp

A static recompilation port of **WWE SmackDown! vs. Raw 2006** (PlayStation 2, PAL `SLES-53676`) to native 64-bit Windows. The game's MIPS R5900 code is translated to C++ ahead of time with [PS2Recomp](https://github.com/ran-j/PS2Recomp), and a custom runtime emulates the PS2 hardware around it: Emotion Engine, VU0/VU1, DMA/GIF/VIF, the Graphics Synthesizer (rendered with Vulkan via [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs)), the IOP and SPU2.

> **This repository contains no game content.** It has no game executable, disc files, assets, recompiled game code, memory dumps, screenshots, music or video. You need your own legally obtained copy of the game. See [Legal](#legal).

**Status:** work in progress. The game boots, plays the intro movies and title, reaches the menus, loads matches and runs wrestler entrances. Matches are not yet playable end to end. Weighted completion is about **37%** (`tools/game_completion.py`); see [Status](#status).

## Features of the runtime
- **Hardware-exact EE FPU.** The COP1 model reproduces real-PS2 results bit for bit (all 901 `ps2autotests` cases pass), including denormal, overflow and divide/sqrt rounding behaviour.
- **VU0/VU1.** VU0 macro mode and a VU1 interpreter with a fast path that models flag, clip, Q/P and XGKICK pipeline latency, validated against PCSX2 captures (`tools/vu1check`).
- **GS rendering.** A GIF/GS front end feeding paraLLEl-GS on Vulkan, with a multi-threaded GS worker, a GIF path arbiter and 60 FPS pacing.
- **IOP and SPU2 HLE.** High-level emulation of the IOP modules and SPU2 audio, plus a native movie (PSS/MPEG) player and IPU decoding.
- **Optional async VIF1/GIF worker** (debug flag 15, off by default) that overlaps VU1/GS work with game logic.
- **Custom music.** The in-game soundtrack list can be replaced by your own tracks, with A-Z ordering and per-game year tags for the entrance music picker.
- **In-game menus.** A settings menu and an F1 debug panel (flags, cheats, probes), plus a live control channel (`tools/svrlive.py`) for scripted tests.
- **Overlay support.** The game's code overlays are recompiled and loaded at runtime, with branch-delay-slot, `LQ`/`SQ` alignment and hidden-entry-point handling.

## Repository layout
| Path | Contents |
|---|---|
| `PS2Recomp/` | The recompiler and runtime (a fork of PS2Recomp) with the SVR06-specific work. Runtime source is in `PS2Recomp/ps2xRuntime/src/lib/` (`gs/`, `vu/`, `Kernel/`, `ps2_memory.cpp`, `ps2_runtime.cpp`, ...). |
| `config/` | Recompiler configuration for the main executable (`svr06_ps2recomp.toml`) and the HLE plan. |
| `tools/` | Python/C++ analysis and diagnostic tools: live control, GIF/VRAM analysis, animation and collision tracing, PCSX2 capture comparison, the VU1 verifier (`vu1check`), completion tracking. |
| `scripts/` | Windows build and run helpers used during development. |
| `docs/` | The changelog (`CL-nnnn`), issue register (`ISS-nnn`), findings (graphics, audio, movies, runtime), the collision/animation maps and completion tracking. |

## Status
Weighted completion by area (re-scored 2026-11-05; scores come from evidence only, see `docs/GAME_COMPLETION.md`):

| Area | Done |
|---|---|
| Boot / intro / title | 70% |
| Main menu and submenus | 43% |
| Exhibition (match setup) | 38% |
| Match types (in-ring) | 22% |
| Match engine subsystems | 33% |
| Entrances | 65% |
| Season / GM mode | 32% |
| Create modes | 26% |
| Audio | 48% |
| Platform layer | 53% |
| **Overall** | **37%** |

What works: boot, intro and title movies, menus, loading into matches and the arena scene, wrestler entrances with music, the custom music system, a locked 60 FPS on the intros and title.

Known open work:
- **Entrance speed.** Entrances run slower than full speed. About two thirds of the game thread is spent interpreting VU1 microprograms. The plan is to optimise the interpreter and compile the hot VU1 programs ahead of time (see `docs/` for the ISS-144 notes).
- **Animation.** Crouched legs in some poses (ISS-139).
- **Collision.** The overlap test, pair query and response dispatch are verified; the response function and the ring/rope bounds are not yet traced.
- **Gameplay logic.** Grapple, strike and reversal logic, crowd, referee and AI are untraced.
- **Audio.** Stock music and sound-effect accuracy still need verification.
- **Cleanup.** The debug probes in the runtime must be removed or gated before a release build.

## Building
Prerequisites: Windows 10/11 x64, CMake, Ninja, Clang with the MSVC toolchain, the Vulkan SDK, Python 3.

1. **Get the game.** Dump your own disc and place the main executable where `config/svr06_ps2recomp.toml` expects it (`assets/SLES_536.76`). Keep the rest of the disc files for runtime use.
2. **Fetch the renderer.**
   `git clone --recursive https://github.com/Arntzen-Software/parallel-gs PS2Recomp/ps2xRuntime/third_party/parallel-gs`
3. **Recompile the game.** Run `ps2xRecomp` with the config. This generates the recompiled code (`PS2Recomp/ps2xRuntime/src/runner/`) and the overlay function tables (`ovl*_functions.h`). They are generated from your copy of the game and are deliberately not committed.
4. **Build the runtime** with CMake and Ninja (Clang). The scripts in `scripts/` show the commands used during development.
5. **Run.** Point the runtime at your disc files. Debug flags can be toggled live with `python tools/svrlive.py flag <bit> 0|1`.

Because the generated sources are not in the repository, a plain `git clone` will not build until step 3 has been done.

## Tools
- `tools/svrlive.py` – control a running game (flags, input, screenshots, memory reads).
- `tools/vu1check` – offline VU1 verifier that replays PCSX2 captures against the interpreter.
- `tools/game_completion.py` – recomputes the completion table from `game_completion.json`.
- `tools/compare_runs.py`, `tools/log_summary.py`, `tools/lookup.py` – run comparison and log analysis.
- `tools/gif*.py`, `tools/vram*.py`, `tools/tex*.py` – GIF stream, VRAM and texture analysis.

## Contributing
Issues and pull requests are welcome, especially for VU1 performance, collision and animation tracing, and audio accuracy. The project aims to match the original console and PCSX2 behaviour one to one by default; enhancements should be opt-in. Please log changes in `docs/CHANGELOG-*.md` as `CL-nnnn` entries and use `ISS-nnn` for issues.

## Legal
- This project is unaffiliated with, and not endorsed by, WWE, THQ, Yuke's, Sony or any other rights holder. "WWE" and "SmackDown! vs. Raw" are trademarks of their respective owners.
- No copyrighted game material is distributed here. You must supply your own legally obtained copy of the game, and you are responsible for complying with the law where you live.
- Do not open issues or pull requests containing game files, dumps, disc images or extracted assets.

## Authors
- **jfussell94** – project owner: direction, testing, requirements and all in-game verification.
- **Claude (Anthropic)** – AI co-author. Most of the code, analysis, tooling and documentation in this repository was written by Claude (Claude Code, Sonnet/Opus models) working under jfussell94's direction. Commits made with Claude carry a `Co-Authored-By: Claude` trailer. All changes were reviewed and run by the project owner.

## Licenses and credits
This project builds on the work of others. Thank you to:

- **[PS2Recomp](https://github.com/ran-j/PS2Recomp)** (ran-j and contributors) – the static recompiler and base runtime this repository is forked from; see `PS2Recomp/LICENSE`.
- **[paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs)** (Arntzen Software / Hans-Kristian Arntzen) – the Vulkan GS renderer, LGPL-3.0, fetched separately (not included). It builds on the Granite engine.
- **[PCSX2](https://github.com/PCSX2/pcsx2)** – the reference emulator. Behaviour, captures and GS/VU1 comparisons were checked against it, and its soft-float work ([PR #12001](https://github.com/PCSX2/pcsx2/pull/12001)) informed the FPU model.
- **[ps2autotests](https://github.com/unknownbrackets/ps2autotests)** (unknownbrackets and contributors) – real-hardware test results used to verify the EE FPU bit for bit.
- **[eefloat](https://www.github.com/hch12907/eefloat)** (hch12907) – reference for Emotion Engine floating-point behaviour.
- **hctp-recomp** – its ChainWorker design and ahead-of-time VU1 compiler approach (GPL-3.0) informed the async VIF1/GIF worker and the VU1 optimisation plan.
- **[ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp)** (exoteric33) – reference for native PS2 recompilation and its performance lessons, together with the r/decomps community write-up on making recompilations beat emulation.
- **ps2tek** – the PS2 hardware reference used throughout.
- **[console-skill](https://github.com/Jakeschincariol/console-skill)** (Jakeschincariol) – the console-porting reference pack used during the work.
- **Montserrat** (The Montserrat Project Authors, SIL Open Font License 1.1) – the font embedded in the in-game menu.
- **Ghidra** (NSA) – used for static analysis of the game executable.

If a source is missing or wrongly credited, please open an issue and it will be fixed.
