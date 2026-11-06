# Plan: Vulkan (paraLLEl-GS) as the default GS backend - 2026-10-28 (planning only, no code changed)

## Why
Today the GS is a software rasterizer (`ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp`) running on CPU threads and shown through raylib/OpenGL. It is slow (part of the 52-59 FPS and the long loads), has open correctness bugs (ISS-103/104/105 paletted alpha/blend) and gives us no path to resolution scale, Linux/Steam Deck or texture packs. Goal: Vulkan GPU backend by default, software GS kept as the reference and automatic fallback.

## What other projects do (sources at the end)
| Project | GS approach | Lessons |
|---|---|---|
| paraLLEl-GS (Themaister, LGPL-3.0+) | Vulkan **compute** GS: GIF register/packed/image-upload API, 4 MiB VRAM in an SSBO, tile binning + ubershader, per-page hazard tracking, SSAA up to 16x, integer nearest upscaling | Not bit-exact, needs subgroup ops, bindless, 16-bit types, timeline semaphores; desktop GPUs are the target; CPU-side per-primitive setup can dominate; missing 32-bit Z, mip/aniso details, de-interlacer |
| BT3-Recomp (PS2Recomp fork) | OpenGL renderer **plus** paraLLEl-GS as an optional default-when-built backend (`renderer=` 0 GL / 1 software / 2 paraLLEl-GS) | Same GIF stream feeds both; VRAM-exact path removes render-target aliasing hacks; ~2x faster than its GL path (2.8 ms CPU / 2.9 ms GPU vs 5.5 / 7.5); presents via fenced Vulkan->CPU readback ring into the GL window (+0.06 ms, +1 frame latency); on Windows some vendor drivers crash during shader compile, so it can use bundled Mesa lavapipe or retry with OpenGL and logs `vulkan-fallback.log`; knobs `PS2X_PGS_SSAA`, `PS2X_PGS_HIRES`, `PS2X_VK_NATIVE` |
| hctp-recomp (Yuke's, our closest relative) | paraLLEl-GS on a Vulkan GPU, AOT-compiled VU1 | Their fixes were in the front end, not the renderer: PSMT4HH/PSMT8H upload sizes, libvu0 stubs, JR/JALR VU1 fix; 32-44 fps demo match still limited by VU1 |
| ICO-recomp | paraLLEl-GS shipped (submodule + shared library), a clean-room Vulkan/D3D12 renderer is experimental and gated behind a parity test | Keep the old renderer as the reference and require a parity gate before changing defaults; do not copy paraLLEl-GS code into an MIT tree |
| Reo (PS2 recomp) | software GS rasterizer today, Vulkan (paraLLEl-GS) planned | Replay PCSX2 GS dumps through the backend to test it without the game |
| Crush Hour PC | D3D11 backend + native RenderWare draws | A native path that skips GS works but is per-game; not our route for a 1:1 port |
| PCSX2 Vulkan/GSdx HW renderer | hardware-rasterizer translation (blend/barrier/texture-cache heuristics) | The known-good accuracy reference for diffing; we use its GS dumps as the oracle |

## Architecture
```
VIF1/GIF arbiter -> GS front end (gs_frontend.cpp: parse GIF tags, PATH1/2/3) -> IGSBackend
                                                       |- SoftwareGS   (current, reference, fallback)
                                                       |- ParallelGS   (new: write_packed / write_register / write_image_upload,
                                                                         vsync -> scanout, readback for FBP reads)
Presenter: phase 1 fenced readback ring -> existing raylib texture (proven by BT3); phase 2 own Vulkan swapchain window
```
* One interface, selected by config `[video] renderer = vulkan | software | auto` (default `auto` = vulkan, falls back to software if device creation, shader compile or first frame fails; log `analysis/vulkan-fallback.log`). `SVR06_GS=software|vulkan` env override. This is the single config layer in the requirements doc.
* Diff mode (`SVR06_GS=both`): feed the same packets to both and compare the final frame hash/PNG per vsync; used to prove parity and find the ISS-103/104/105 class of bugs.
* GS dump record/replay (we already have `SVR06_GSDUMPTICK/LEN`): replay our own dumps through ParallelGS offline (no game boot needed) - the 8-minute load is not needed for renderer work.

## Requirements to check on the target machine
Vulkan 1.1+ with compute, subgroup ballot/shuffle/clustered ops, descriptor indexing (bindless), 16-bit storage/arithmetic, timeline semaphores, enough host-visible memory for the readback ring. The development PC has an AMD Radeon (Adrenalin installed), which is the best-supported vendor for paraLLEl-GS (author tested on AMD RX 7600). Intel/NVIDIA/mobile are unverified.

## Phases and exit criteria
| Phase | Work | Exit criterion |
|---|---|---|
| P0 spike (1-2 days) | Build paraLLEl-GS + Granite as submodule/shared lib with the project's clang/MSVC + Ninja; run its replayer on our existing GS dumps (`recomp_gs_7000.bin` + `recomp_vram_7000.bin`) | Title + loading + demo frames rendered offline, compared by eye with the software output |
| P1 integration | `IGSBackend` interface extracted from `gs_frontend`; ParallelGS adapter (register writes, packed/image data, VSync/scanout, framebuffer readback hook for games that read VRAM); readback-ring presenter | Boots to title on Vulkan, no crashes over a full demo loop |
| P2 parity | `both` diff mode on title, menu, demo match; fix front-end differences (PSMT4HL/HH, CLUT, TEXA, alpha) | Frame diff within tolerance on a scripted route; ISS-103/104/105 disappear or are explained |
| P3 default | `renderer=auto` -> vulkan; fallback chain, `vulkan-fallback.log`, Windows driver-crash guard (retry software; optional lavapipe like BT3) | 3 clean runs; fallback verified by forcing a failure |
| P4 performance | GS on its own thread, then async with VU1 (after the planned AOT-VU1 work); frame pacing 50->60 Hz handling | Steady 60 FPS in the demo match (ISS-106) |
| P5 quality options | Internal resolution / SSAA, widescreen and 4:3 handling in the presenter, own swapchain window, resize without pausing (ISS-107) | Options persist through the shared config layer |
| P6 extras | Texture pack hooks (hash of TEX0+CLUT upload at the VRAM-upload call), shader/pipeline warm-up launcher option, Linux/Steam Deck test | Matches the approved wishlist |

## Risks and how we handle them
1. **Not bit-exact**: keep SoftwareGS as the reference and `both` mode; accept visual-equivalent output, document known diffs.
2. **Missing features**: 32-bit Z, mip details, de-interlacing; test if SvR06 uses them (it uses Z24/Z32 depending on scene - check FRAME/ZBUF psm in dumps in P0).
3. **Driver crashes at shader compile (Windows)**: fallback chain, on-disk pipeline cache, lavapipe option.
4. **Stream differences**: libgs is HLE-stubbed so our GIF stream differs from PCSX2's. The planned restore of the original libgs code should land before parity testing (otherwise parity noise).
5. **Readback and latency**: VRAM reads by the game (screen effects) need the sync path; readback ring adds one frame.
6. **CPU setup cost per primitive**: GS thread plus the AOT VU1 compile; VU1 interpretation remains the main frame cost regardless of backend.
7. **Licensing**: paraLLEl-GS is LGPL-3.0+; link as a shared library and keep notices (the project already carries GPL code via the IPU port).

## Decisions (user, 2026-10-28)
1. **Vulkan only for now**: the user-facing renderer is Vulkan; the software GS stays in the tree only as the developer reference / diff oracle (not offered as a player option; no silent software fallback for play, a failure shows a clear error + log).
2. **Submodules approved**: `Arntzen-Software/parallel-gs` (the real upstream; `Themaister/parallel-gs` does not exist) with its `Granite` submodule at `PS2Recomp/ps2xRuntime/third_party/parallel-gs`.
3. **Hardware target (my choice)**: minimum = any GPU with Vulkan 1.1+, compute, subgroup ballot/shuffle, descriptor indexing, 16-bit storage and timeline semaphores - NVIDIA GTX 900/10-series (Maxwell/Pascal) and newer, AMD RX 400/500 (GCN4) and newer, Intel Arc/Xe; 8 GB RAM, 2 GB VRAM. Recommended for 2x-4x resolution/SSAA: RTX 2000+/RX 5000+. Dev machine: AMD Radeon RX 6700, Vulkan 1.4, AMD proprietary driver. NVIDIA is untested on our side: it needs a test pass (driver shader-compile crashes were reported on some Windows vendor drivers). The device-creation check refuses GPUs missing a required feature and tells the player why. 1:1 mode = native 1x resolution, no SSAA.

## Sources
[paraLLEl-GS design article](https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation) · [BT3-Recomp](https://raw.githubusercontent.com/z3xox/BT3-Recomp/HEAD/README.md) · [ICO-recomp](https://raw.githubusercontent.com/nathanialf/ico-recomp/HEAD/README.md) · [Reo](https://raw.githubusercontent.com/sp00nznet/reo/HEAD/README.md) · [hctp-recomp](https://github.com/BigDaddyJRich/hctp-recomp) · [WWE Crush Hour PC](https://github.com/KaikoClanworth1/wwe-crush-hour-pc) · [Urban Reign recomp](https://github.com/thiagordsfx/urban-reign-recomp) · [LRPS2 Vulkan LLE renderer](https://scribe.disroot.org/post/1683591) · [PCSX2 GSdx](https://wiki.pcsx2.net/GSDX)

## POLICY (user, 2026-10-28 11:42): OpenGL is end-of-life
* From now on **no new code, fixes or features target OpenGL**; OpenGL is not a supported or offered renderer.
* The only approved renderers: **Vulkan via paraLLEl-GS** (player-facing) and the **software GS** (developer reference / diff oracle only, never shown to players).
* Today's OpenGL use is the raylib presenter (window + texture upload). Replacement: Vulkan presentation (readback ring into a window as a stop-gap during P1, then own Vulkan swapchain window in P5 via SDL3 which ships inside Granite's third_party). raylib/GLFW-OpenGL window code is removed as soon as the Vulkan presenter works.
* **Removal milestone:** when the game is confirmed working on Vulkan (P3 exit: 3 clean runs on title -> menu -> demo match), delete ALL OpenGL code (raylib GL presenter, GL loaders, GL shaders, GL config keys/env vars, docs mentions) in one cleaning commit, and add a grep check to `tools/check_clean.py` that fails if `GL/gl`, `glad`, `rlgl`, `raylib` GL calls reappear.
* Until then OpenGL code is frozen (bug fixes only if it blocks testing) and tracked as ISS-120 "remove OpenGL".

## P0 result (2026-10-28 12:00): paraLLEl-GS builds with our toolchain
* Configured/built with `scripts/pgs_build.cmd` (vcvars64 + clang-cl + Ninja, `-DPARALLEL_GS_STANDALONE=ON`, Release) into `PS2Recomp/build_pgs`: 90/90 steps, exit 0; outputs `gs/parallel-gs.lib`, `dump/parallel-gs-dump.lib`, `analog/analog-video.lib`, `sandbox/parallel-gs-sandbox.exe`, `analog/analog-sandbox.exe`. Standalone mode = no SDL window, no runtime shader compiler (SPIR-V is compiled at build time, so no shader-compile stutter and no glslang needed at run time).
* Public API (`gs/gs_interface.hpp`, class `GSInterface`) maps one-to-one onto what our GS front end already receives:
  | paraLLEl-GS | our side |
  |---|---|
  | `init(Vulkan::Device*, GSOptions)` | create Vulkan device (Granite `Vulkan::Context/Device`), options: super-sampling, ordered grid |
  | `gif_transfer(path, data, size)` | raw GIF packets for PATH1/2/3 straight from `ps2_gif_arbiter` (no re-parsing needed) |
  | `write_register(addr, payload)`, `get_register_state()` | A+D writes / direct GS register pokes |
  | `get_priv_register_state()` | privileged regs (PMODE, DISPFB, DISPLAY, SMODE, CSR...) written by libgs/kernel code |
  | `map_vram_write/end_vram_write/map_vram_read` | host VRAM access (our IPU/FMV upload, VRAM dumps, GS->EE readbacks) |
  | `vsync(VSyncInfo)` -> `ScanoutResult` | per-vsync scanout image (deinterlace, CRTC offsets, overscan options) that the presenter shows |
  | `set_signal_interface(SignalInterface)` | GS SIGNAL/FINISH/LABEL events -> our CSR/interrupt emulation |
  | `set_hacks(Hacks)` | disable_mipmaps, unsynced_readbacks, backbuffer_promotion, allow_blend_demote (quality/perf switches) |
* Next (P1): wrap it in `IGSBackend`, create the Vulkan device through Granite's `Vulkan::Context` (headless first, readback ring into the existing window), feed `gif_transfer` from the GIF arbiter, call `vsync` from our vsync tick, route SIGNAL/FINISH.

## P1 result (2026-10-28 13:25)
Bridge builds and links (0 errors); `[gsvk] paraLLEl-GS ready`, scanout images produced every vsync after synthesizing SMODE1 (PAL) and 4:1 horizontal downscale. Remaining for P1 exit: confirm picture on title/menu/demo, PAL 640x512 output, movies, SIGNAL/FINISH routing, VRAM readback paths.

## P3 step 1 (2026-10-28 evening, CL-0054)
Vulkan is now the default backend in code (`SVR06_GS=software` for the dev reference). Awaiting rebuild and 3 clean title -> menu -> demo runs before the OpenGL removal milestone (ISS-120).
