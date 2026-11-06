# SVR-06 recomp

Static recompilation port of *WWE SmackDown! vs. Raw 2006* (PS2, PAL) to native 64-bit Windows, built on [PS2Recomp](https://github.com/ran-j/PS2Recomp) with a Vulkan (paraLLEl-GS) renderer.

**This repository contains no game content.** You need your own legally obtained copy of the game. It does not include the game executable, disc files or assets, recompiled game code, memory dumps, captures, screenshots, music or video.

## What is here
- `PS2Recomp/` – the recompiler and runtime (fork of PS2Recomp, see its `LICENSE`) with the SVR06-specific work: EE FPU hardware-exact model, VU0/VU1 interpreter, GS/Vulkan front end, IOP/SPU2 HLE, custom-music system, settings menu, debug panel.
- `config/` – recompiler configuration for the game's main executable (`SLES_536.76`).
- `tools/` – analysis and diagnostic scripts (Python/C++).
- `scripts/` – Windows build/run helpers.
- `docs/` – changelog (`CL-nnnn`), issue register (`ISS-nnn`), findings and completion tracking.

## Building
1. Dump your own disc; place the main executable where `config/svr06_ps2recomp.toml` expects it (`assets/SLES_536.76`).
2. Check out the renderer dependency into `PS2Recomp/ps2xRuntime/third_party/parallel-gs`:
   `git clone --recursive https://github.com/Arntzen-Software/parallel-gs PS2Recomp/ps2xRuntime/third_party/parallel-gs`
3. Run the recompiler with the config to generate the game code (`ps2xRecomp`), producing `PS2Recomp/ps2xRuntime/src/runner/` and the overlay function tables (`ovl*_functions.h`). These are generated from your executable and are deliberately not committed.
4. Build with CMake/Ninja (Clang). See `scripts/` for the helpers used during development.

Because the generated function tables are not included, step 3 must be done before the first build.

## Licenses
PS2Recomp and the runtime: see `PS2Recomp/LICENSE`. paraLLEl-GS: LGPL-3.0. Embedded menu font: Montserrat, SIL OFL 1.1. Ideas and some structure for the async VIF1 worker follow hctp-recomp (GPL-3.0). "WWE SmackDown! vs. Raw" is a trademark of its respective owners; this project is unaffiliated.
