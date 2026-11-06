# SVR06 PC port: change log

## Version history

Pre-1.0 alpha. Version numbers are assigned to ranges of change-log entries (semantic-ish: minor = a milestone, patch = a batch of fixes). Current: **v0.5.3-alpha** (CL-0121). 1.0.0 = matches PCSX2 on a full play-through. Each `## CL-nnnn` heading in the change logs carries its `[vX.Y.Z]` tag.

| version | CL range | dates | summary |
|---|---|---|---|
| v0.1.0 | CL-0001..CL-0005 | 2026-10-20..23 | Foundation: ELF analysis, PS2Recomp build, runtime bring-up, boots to the Yuke's logo. |
| v0.2.0 | CL-0006..CL-0034 | 2026-10-23..27 | Overlays recompiled, native movie player, recompiler/MMI/branch fixes, intro and title reached. |
| v0.3.0 | CL-0035..CL-0052 | 2026-10-27..28 | MMI/FPU/VU0 correctness (HI/LO, flags, trunc), SPU2 model, diagnostics. |
| v0.4.0 | CL-0053..CL-0077 | 2026-10-28..29 | Vulkan (paraLLEl-GS) default renderer, VU clamp/Q fixes, SPR DMA chain, demo-freeze guard, collision logging. |
| v0.4.1 | CL-0078..CL-0099 | 2026-10-30..11-01 | PCSX2 ground-truth comparison, animation/skeleton work, entrance fixes, VU1 speed + runaway safeguard. |
| v0.5.0 | CL-0100..CL-0110 | 2026-11-02..03 | VU1 JR/JALR fix (Locker Room), SoH-style F1 menu, commentary audio, renderer option, ISS-130 texture fix, per-player controllers, bit-exact EE FPU. |
| v0.5.1 | CL-0111..CL-0115 | 2026-11-03 | Title Press START pill, locked 60 FPS on intros/title, fenced readback queue, no pink frames. |
| v0.5.2 | CL-0116..CL-0120 | 2026-11-03 | Title-hold release, fade fixes, custom music replacements + now-playing popup, music-slot findings. |
| v0.5.3 | CL-0121 | 2026-11-04 | Extra entrance songs in the game's own Create-an-Entrance list (A-Z, "none" last, game-year tags). |

(Entries CL-0078..0099 are recorded in the project docs `claude/changelog-CL-00xx-*.md` and the older mirror.)

Numbering: `CL-nnnn`, assigned in the order changes were made (older entries are numbered retroactively from the project history; dates before 2026-10-23 are approximate because they come from file dates and the earlier hand-off). Every new change gets the next free number; the next free number is recorded in the memory file (`areas/svr06-port-log.md`) and at the bottom of this file. Entries reference issue ids (`ISS-nnn`) from `findings/issues-and-remedies.md`.

Columns: id | date | area | change | files / addresses | issue | verified?

## Foundation (2026-10-20 .. 2026-10-22, approximate)
| id | date | area | change | files | issue | verified |
|---|---|---|---|---|---|---|
| CL-0001 | 10-20 | prep | ELF parsed (unencrypted, entry 0x100008, one 2.6 MB code segment, 9,832 function candidates, 169 syscall stubs). Disc indexed (102 DPAC, 26 AFS, 20 IRX). Config/HLE plan/setup scripts scaffolded. | `tools/analyze_elf.py`, `tools/index_assets.py`, `analysis/asset_index.json`, `analysis/elf_summary.json` | | yes |
| CL-0002 | 10-20..22 | recompiler | PS2Recomp built; analyzer found 246 SDK stubs, 7 jump tables, 54 patches; code ends at 0x30C4D0, higher addresses are data (214 `skip` entries). 7,226 functions -> 7,240 C++ files, 0 errors, ~2,000 control-flow warnings. | `config/svr06_ps2recomp.toml`, `PS2Recomp/ps2xRuntime/src/runner/` | | yes |
| CL-0003 | 10-21..22 | runtime | Runtime bring-up: 32 MB memory, syscalls, SIF RPC/CD HLE, cooperative thread/semaphore scheduler, software GS rasterizer, VU1 interpreter, native audio hooks; boots to the Yuke's logo. | `Kernel/EeScheduler.cpp`, `Kernel/Syscalls/*`, `lib/gs/*`, `lib/vu/*`, `ps2_audio.cpp` | | yes |
| CL-0004 | 10-22 | overlays | First overlay bank system: `svr06ovl::addBank/addFunc/find`, banks A/D/L/M/S recompiled from RAM slices; signature words verified against RAM. | `overrides/svr06_overlay.cpp/.h`, `overrides/ovlD_*.cpp`, `svr06_ovl_demo.cpp` | | partial |
| CL-0005 | 10-22..23 | reference | PCSX2 menu-walk capture (inputs, logs, savestate) for comparing real behaviour. | `analysis/pcsx2_menu_walk_*`, `analysis/ref/` | | yes |

## 2026-10-23 .. 2026-10-25
| id | date | area | change | files | issue | verified |
|---|---|---|---|---|---|---|
| CL-0006 | 10-23 | movies | Native movie player (FFmpeg decodes `.sfd`: mpeg1video + adpcm_adx from AFS under MOVIEP). Hooks `movieNameHook` @0x1498B8 and `movieSkipHook` @0x19BCC0. | `Kernel/Stubs/SvrMovie.cpp`, `overrides/svr06_overrides.cpp` | ISS-010 | user confirmed |
| CL-0007 | 10-23/24 | overlays | `svr06ovl::find()` resumes mid-function by scanning back up to 0x10000. | `overrides/svr06_overlay.cpp` | ISS-012 | yes |
| CL-0008 | 10-24 | movies | `adxGetTimeHook` @0x18A188 supplies real elapsed ms to the game's movie clock (not for the title movie); `[mend]` probes. | `overrides/svr06_overrides.cpp` | ISS-011 | yes |
| CL-0009 | 10-24/25 | kernel | Thread syscalls return the thread id on success. | `Kernel/Syscalls/Thread.cpp` (+`.bak_ret`) | ISS-013 | yes |

## 2026-10-26
| id | date | area | change | files | issue | verified |
|---|---|---|---|---|---|---|
| CL-0010 | 10-26 | diagnostics | Thread-syscall logging: `[start]`, `[sys]` (chatty calls capped), `[thread-state]`; `[sema] spin` prints waiter info. | `EeScheduler.cpp`, `Syscalls/*` | ISS-001 | yes |
| CL-0011 | 10-26 | overlays | All 11 overlays decoded from `BIN\BIN.PAC` (DPAC -> PAC -> BPE -> MWo3); 10 recompiled as banks `ovx<Name>` (6,922 functions) with `tools/ovl_recomp.py` (+`ovl_recomp_all.sh`) in the device VM (Linux `ps2_recomp` in `~/prc`). | `tools/ovl_recomp.py`, `overrides/svr06_ovx*.cpp`, `findings/overlays.md` | ISS-001 | yes |
| CL-0012 | 10-26 | recompiler | JAL to an internal target is always a real call. 524 call sites / 355 functions; 160 main-ELF files changed. | `ps2xRecomp/src/lib/control_flow_emitter.cpp` (+`.bak_jalfix`), `analysis/runner_backup_jalfix/` | ISS-002 | yes |
| CL-0013 | 10-26 | runtime | `PMFHL.LW/UW/SLW/LH/SH` rewritten per EE manual; pipeline 1 from `ctx->hi1/lo1`. | `ps2xRuntime/include/ps2_runtime_macros.h` (+`analysis/ps2_runtime_macros.h.bak_pmfhl`) | ISS-003 | unit-tested |
| CL-0014 | 10-26 | overlays | Overlay image length fixed (+0x40 header). | `tools/ovl_recomp.py` | ISS-004 | yes |
| CL-0015 | 10-26 | recompiler | Regenerated main ELF picked up the newer VU0 `SQI` fix (writes VU0 data memory at 0x11004000+addr). | `ps2xRecomp` | | yes |
| CL-0016 | 10-26 | build | `fast_rebuild.ps1`: single-build guard (kills ninja/cmake/clang/link/svr06run, clears `.ninja_log.restat`), progress bar with n-of-m plan; `FAST_REBUILD_AND_RUN.cmd`; `RUN_LOGGED.cmd`. | `scripts/` | ISS-014 | yes |

## 2026-10-27
| id | date | area | change | files | issue | verified |
|---|---|---|---|---|---|---|
| CL-0017 | 10-27 | recompiler | BLEZ/BGTZ/BLTZ/BGEZ (+likely/AL) use `GPR_S64`. 1,659 main-ELF functions regenerated; overlay banks re-run. | `control_flow_emitter.cpp::conditionalBranchExpression`, `analysis/runner_backup_main_s64/` | ISS-005 | yes (movie decoder no longer hangs) |
| CL-0018 | 10-27 | config | 50 missing main-ELF `entry_points` added. | `config/svr06_ps2recomp.toml` | ISS-006 | yes |
| CL-0019 | 10-27 | overlays | `ovl_recomp.py` scans `jal`+`j` in main ELF, own overlay and other-slot overlays; 1,890 files updated. | `tools/ovl_recomp.py`, `analysis/runner_backup_ovx_labels/` | ISS-007 | yes |
| CL-0020 | 10-27 | overlays/config | `ovl_recomp.py` v3 hidden starts + code-pointer scan + `kLabels[]` (`svr06ovl::addLabels`); 6,942 overlay files; main ELF +1,800 hidden `entry_points` (1,478 files, 1,777 register lines). | `tools/ovl_recomp.py`, `config/svr06_ps2recomp.toml`, `analysis/runner_backup_main_hidden/` | ISS-008 | yes (match renders) |
| CL-0021 | 10-27 | runtime | Guest heap hard limit 0x01F00000 -> 0x01FC0000; async-callback stacks moved to [0x1FC0000,0x1FF0000). | `ps2xRuntime/src/lib/ps2_runtime.cpp`, `Kernel/Syscalls/System.cpp`, `EeScheduler.cpp` (`*.bak_heaplimit`) | ISS-009 | yes |
| CL-0022 | 10-27 | diagnostics | Heap instrumentation: `gaWatch`, `resLoadWatch/resCopyWatch`, `heapAllocWatch/heapFreeWatch`, `[gabig]`, `[gaalloc]`, `[livereport]`; `liveDel` free-history (`g_freed`, cap 400,000), `svr06DescribeAddr` (`[addrinfo]`) and its call in the guest-branch missing-target reporter. | `overrides/svr06_overrides.cpp`, `ps2_runtime.cpp` (+`.bak_descr`) | ISS-009/ISS-100 | n/a (debug) |
| CL-0023 | 10-27 | diagnostics | RAM-corruption canary in `startScenePoll` (`[canary]`, thread snapshots, 3 RAM scans for lane-3 bit-31). | `overrides/svr06_overrides.cpp` | ISS-100 | not yet fired |
| CL-0024 | 10-27 | diagnostics | GS dump window extended (`g_svr06DumpActive`); VU1 `[vudump]` (code + 24 data snapshots) in `ps2_vu1_core.cpp` (+`.bak_vudump`); tools `gifdump2.py`, `gifdump3.py`, `~/vu/vudis.py` (VM). | `gs_frontend.cpp`, `ps2_vu1_core.cpp`, `tools/` | ISS-101/103 | yes |
| CL-0025 | 10-27 | diagnostics | `[glyphdbg]` in `GSCpuBackend::SampleTexture` for T4HL `tbp0 0x1800` during the dump window; `run.cmd` `SVR06_GSDUMPLEN` 4 -> 20. | `lib/gs/gs_cpu_backend.cpp` (+`.bak_glyphdbg`), `scripts/run.cmd` | ISS-103 | build in progress |
| CL-0027 | 10-27 | diagnostics | `[vifmat]` in the VIF1 interpreter: logs UNPACKs that write VU1 data qwords 0x3f4..0x3f7 with guest source address and first 16 bytes (first 200). | `ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp` (+`.bak_vifmat`) | ISS-101 | build in progress |
| CL-0026 | 10-27 | docs | Documentation set created: issue register, change log, completion tracking, analyses, graphics/audio/movie/system findings, requirements and plan. Memory index created. | project docs, memory | | yes |

## Planned (not yet made; numbers reserved)
| id | planned change | issue |
|---|---|---|
| CL-0028 | VIF1 UNPACK V4-5: shift R/G/B by 3 and A by 7; verify V2/V3 missing-component values against PCSX2. | ISS-117 |
| CL-0030 | VU0 `VDIV/VSQRT/VRSQRT` PS2 semantics in the translator; regenerate affected main-ELF functions and overlay banks. | ISS-102 |
| CL-0031 | Fix glyph-box (paletted alpha / CLUT / texture-cache) once `[glyphdbg]` shows the cause. | ISS-103/104 |
| CL-0032 | Identify and fix the RAM lane-3 bit-31 writer. | ISS-100 |
| CL-0033 | Async VU1/GS, loader work off the presenting thread, window-drag fix. | ISS-106/107 |
| CL-0034 | Remove debug instrumentation; `check_clean.py`; final `docs\FINDINGS.md`. | ISS-114 |

Next free number: CL-0035 (CL-0027..0029 reserved for small fixes found while doing the above).
