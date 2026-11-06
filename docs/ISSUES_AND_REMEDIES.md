# SVR06 PC port: issue register and remedies

Project: native static recompilation of WWE SmackDown! vs. Raw 2006 (PAL, SLES-53676, boot ELF `SLES_536.76`) with PS2Recomp. This file is the single place to look up "what broke, why, and how it was fixed (or what to try next)". Written 2026-10-27. It contains no game data.

How to read it
- ID scheme: `ISS-nnn` for issues (solved or open). Each row has Symptom, Root cause, Remedy, Status, Where (files/addresses), Changelog id (`CL-nnnn`, see `changelog/CHANGELOG.md`).
- Status values: FIXED (verified in a run), FIXED-UNVERIFIED (change made, not yet confirmed), OPEN (known, not fixed), INVESTIGATING (cause not yet known).
- Priority rule from the user: critical issues first, then minor. Order inside the "open" section follows that.
- Detailed investigations that do not fit a table row are in `findings/analyses.md` (A-nn ids).

Project rules that apply to every remedy (stated by the user)
- Never upload game data (live_ram.bin, live_vram.bin, ISO/AFS/PAC contents, ELF, generated game code, decoded overlay images). Run `python tools\check_clean.py` before publishing.
- Never clean-build. Never edit generated runner `sub_*.cpp`; fix the recompiler, the TOML, the runtime or `overrides/`. The exception is a deliberate regeneration through the recompiler.
- Header edits: allowed when needed (user OK given 2026-10-27, "edit header if you have to"). Keep a `.bak_*` copy next to the file and log it in the changelog.
- No injected or scripted input and no run-time limits/timers in shipped builds (the live console `press` command is for testing only).
- Base game must be 1:1 with the original; extended features go behind an option (see `status/requirements-and-plan.md`).

---

## 1. Solved issues

### ISS-001 Stall after the logo: missing overlay code
- Symptom: after the logo movie the game never advanced (non-deterministic across runs); later, `[guest-branch:missing-target]` at `0x59E680` and `0x5A2780`.
- Root cause: game code lives in overlays loaded from `BIN\BIN.PAC` at run time (lower slot 0x4D2C00, upper slot 0x5A2700). Most overlays had never been recompiled. First missing: `0x59E680` (a Menu.bin static constructor called from `0x11B3A4`), then `0x5A2780` (Title.bin entry, called from main code `0x207DDC`).
- Remedy: decode all 11 overlays from `BIN.PAC` (DPAC entry, `PAC ` wrapper, `BPE ` at +16, MWo3 image) inside the device VM; recompile 10 of them (all except `dnas.bin`, which is online DNAS auth and looks encrypted) as banks `ovx<Name>` with `tools/ovl_recomp.py`; register through `svr06ovl::addBank`.
- Status: FIXED. Where: `tools/ovl_recomp.py`, `overrides/svr06_ovx*.cpp`, `findings/overlays.md`. Changelog: CL-0011.
- Note: thread 6 (`0x14B6A8`, prio 8) being Suspended at entry is NORMAL (the game resumes it only inside a priority-1 critical section). It was a red herring for a long time.

### ISS-002 JAL to a target inside the current function emitted as a local `goto`
- Symptom: main thread died right after the title movie (`[sched] REAL thread 1 finished (pc=0)`), path Menu.bin -> `0x1E0AC0` -> `jal 0x1E0A50`, both inside `sub_001E0A50`.
- Root cause: `ps2xRecomp/src/lib/control_flow_emitter.cpp`, `emitStaticJump`: when two real functions are merged into one generated C++ function (or a function recurses), `jal X` became `goto label_X`; the callee's `jr $ra` then returned from the whole C++ function.
- Remedy: only non-call jumps may become local gotos: `if (kind != StaticBranchKind::Call && isInternalTarget(target))`. 524 call sites in 355 functions; 160 main-ELF files changed; overlays re-recompiled. Backups `control_flow_emitter.cpp.bak_jalfix`, `analysis/runner_backup_jalfix/`.
- Status: FIXED. Changelog: CL-0012.

### ISS-003 `PMFHL.*` macros wrong (soft-float / 64-bit multiply broken)
- Symptom: `[muldf-zero]`, endless double-precision math on the loading screen, loading never finished.
- Root cause: `ps2xRuntime/include/ps2_runtime_macros.h`: `PMFHL.LW` must return `{LO.w0, HI.w0, LO1.w0, HI1.w0}`; the old macro returned `{LO sign-extended, HI}`. The game's soft-float (`0x13B310`) and dtoa/bigint code use `multu a,b; pmfhl.lw rd`.
- Remedy: rewrote LW, UW, SLW, LH, SH per the EE manual; pipeline 1 read from `ctx->hi1/lo1`. Unit-tested in the VM (`multu 0xDEADBEEF*0x12345678` -> exact 64-bit product). Backup `analysis/ps2_runtime_macros.h.bak_pmfhl`.
- Status: FIXED. Still open nearby: `PMTHL.LW` (recompiler `translatePMTHLInstruction`) ignores pipeline 1 and sign-extends (minor, ISS-030). Changelog: CL-0013.

### ISS-004 Overlay images cut 0x40 bytes short
- Symptom: `game.bin` static constructor at `0x541940` missing.
- Root cause: an MWo3 image is the 0x40-byte header + text + data; the tool used text + data from the base.
- Remedy: `hi = base + 0x40 + text + data` in `tools/ovl_recomp.py`. Status: FIXED. Changelog: CL-0014.

### ISS-005 Conditional branches on 64-bit registers compared only the low 32 bits
- Symptom: libmpeg's post-FDEC wait loop at `0x121328` (`ld IPU_CMD; bgezl`) never exited; the movie decoder hung after the logo.
- Root cause: `control_flow_emitter.cpp`, `conditionalBranchExpression()` emitted `GPR_S32(...)>=0` for BLEZ/BGTZ/BLTZ/BGEZ (+likely/AL variants). The EE compares the full 64-bit register. They differ when bit 31 != bit 63 (zero-extended `ld`/`lwu` of hardware registers; IPU_CMD bit 63 = busy).
- Remedy: use `GPR_S64`. 1,659 main-ELF functions regenerated (backup `analysis/runner_backup_main_s64/`); all overlay banks re-run through `ovl_recomp.py`.
- Status: FIXED. Changelog: CL-0017.

### ISS-006 Direct JAL/J to addresses that are not function entries -> `[guest-branch:missing-target]`
- Symptom: after Start, NOW LOADING forever; main EE thread Dormant (`st=5`); `JAL source=0x4d60c0 target=0x10d320`. `0x10D320` (6-instruction Vec4 constructor) sits inside `sub_0010D110`.
- Root cause: no entry point for the address, so the containing function had no `case` for it.
- Remedy: added 50 `entry_points` in `config/svr06_ps2recomp.toml` (found by scanning main-ELF `jal`/`j` cross-function targets and overlay `jal`/`j` targets into the main ELF). `0x126BE0` and `0x230480` are intentionally left to hand-written overrides.
- How to find more: for every `jal` target in main and overlay code, check it against `register_functions.cpp`. Missing ones inside the main-ELF range are candidates.
- Status: FIXED. Changelog: CL-0018.

### ISS-007 Overlay banks: mid-function entry points had no `case` label
- Symptom: `jal 0x5A2B90` (6-instruction leaf that sets a callback) ran the START of `ovxMainGame_005A29C0`, which freed a never-initialised slot -> `JALR` through garbage -> `target=0x26100001`, main thread dead.
- Root cause: `svr06ovl::find()` resumes a non-entry address inside the nearest preceding function, which dispatches on `ctx->pc`; a missing `case` falls through to the function start. `ovl_recomp.py` only collected `jal` targets from the main ELF and the overlay itself, never from overlays of the other slot, and ignored `j` tail jumps.
- Remedy: scan `jal` + `j` in the main ELF, the overlay, and every overlay of the other slot (lower vs upper slot; same-slot overlays are never co-resident). 1,890 files updated (backup `analysis/runner_backup_ovx_labels/`).
- Status: FIXED. Changelog: CL-0019.

### ISS-008 Hidden function starts (vtable-only leaf functions) ran from the wrong place
- Symptom: cage build `0x519F90` re-ran; MatchSM stuck before state 3.
- Root cause: leaf functions after `jr ra`+nops reached only through pointers had no entry, same mechanism as ISS-007.
- Remedy: `ovl_recomp.py` v3 adds hidden starts (16-byte aligned after `jr ra`/`j`+nop), a lui/addiu code-pointer scan, `kLabels[]` registered via `svr06ovl::addLabels` (all 10 banks regenerated, 6,942 files). Main ELF: 1,800 hidden starts added to `entry_points` (1,478 files + 1,777 register lines merged; backup `analysis/runner_backup_main_hidden/`). The mpeg area `0x123000-0x127000` is intentionally left as it was (hand-modified).
- Status: FIXED (MatchSM reaches state 3, match renders). Changelog: CL-0020.

### ISS-009 Guest heap about 1 MB too small -> OOM and dead main thread at match load
- Symptom: `[gaalloc] -> NULL` for tiny `new`s (0x19C4, 0x650, 0x80), `JALR` through NULL (`source=0x1b07a4 target=0x0`), main thread Dormant.
- Root cause: the runtime capped the heap at `0x01F00000` (`kGuestHeapHardLimit` in `ps2_runtime.cpp`; `kDefaultGuestHeapEnd` x2 in `Kernel/Syscalls/System.cpp`). On the real console the heap ends at the stack bottom `0x1FF0000` (crt0 `SetupHeap(0x62A180,-1)`). Arena 15 (main) was full with the 14 MB `GAME` motion block resident. The retail build has no purge callback (`0x11A580` is `jr ra; move v0,0`).
- Remedy: hard limit `0x01FC0000`; async-callback stacks (16 KB each) moved into `[0x1FC0000,0x1FF0000)` (`m_asyncCallbackStackTop = 0x1FF0000`). Backups `*.bak_heaplimit`. TODO: if "EE invocation stack space exhausted" appears, enlarge that region; ideal is the real `0x1FF0000` limit once async stacks live outside guest RAM.
- Status: FIXED. Changelog: CL-0021. Detail: `findings/heap-limit-and-hidden-starts-2026-10-27.md`.

### ISS-010 Movie blocker: `E211151: ADXF_GetFnameRangeEx() faild.`
- Root cause: ADXF partition table at `0x3129d0` (ptid*4) all zero because the IOP-side ADXF read never ran under HLE.
- Remedy: native movie player (`Kernel/Stubs/SvrMovie.cpp`, FFmpeg decodes `.sfd` MPEG-2 PS + adpcm_adx), hooks `movieNameHook` @ `0x1498B8`, `movieSkipHook` @ `0x19BCC0`. See `findings/movie-findings.md`.
- Status: FIXED (movies play, natural end works, Start skips, title movie holds last frame). Changelog: CL-0006.

### ISS-011 Movie never reaches its natural end
- Root cause: the game's movie clock (time provider `0x18A188`) never advanced because the IOP/ADX path is not emulated.
- Remedy: `adxGetTimeHook` @ `0x18A188` returns real elapsed ms (`*a1=ms,*a2=1`), not applied to the title movie (it must wait for Start). Status: FIXED. Changelog: CL-0008.

### ISS-012 Demo load freeze at `0x4EBC54` (jump into the middle of `sub_004EAF50`)
- Remedy: `svr06ovl::find()` scans back up to 0x10000 for the nearest preceding table entry and resumes mid-function. Status: FIXED (superseded by the label mechanism in ISS-007/008). Changelog: CL-0007.

### ISS-013 Thread syscalls returned 0 instead of the thread id
- Rule found: `ResumeThread/WakeupThread/StartThread/...` must return the thread id on success (the game does `if (ResumeThread(tid)==tid) WakeupThread(tid)` every frame). Implemented in `Kernel/Syscalls/Thread.cpp` (backup `Thread.cpp.bak_ret`). Status: FIXED.

### ISS-014 Build system pitfalls
- Two builds at once -> ninja "failed recompaction: Permission denied". `fast_rebuild.ps1` now kills stray ninja/cmake/clang/link/svr06run and clears `.ninja_log.restat` before building (CL-0016).
- `fast_rebuild.ps1` copies `svr06-recomp\overrides\*.cpp` over `PS2Recomp\ps2xRuntime\src\runner\` and `overrides\*.h` over `ps2xRuntime\include\` on EVERY build. Edits made only in the runner/include copy are silently reverted. Generated overlay files must be written into `overrides\`.
- Use build dir `PS2Recomp\build_svr06b_clang` (clang-cl + Ninja, unity build). `build_svr06b` is broken (CMake mixes Clang and MSVC).
- Wide `grep -r` over the runner/overrides dirs hangs (120 s tool limit). Grep specific files; `grep -h "^ *// 0x" sub_XXXXXXXX_0xXXXXXXXX.cpp` prints the original MIPS.
- Bash tool cannot see the VM mount; use `device_bash` for project files.

### ISS-015 Runtime HLE pitfalls (found during bring-up)
- `loadModuleBuffer` pointers are IOP-side addresses. `sceCdSearchFile` must be HLE'd. fileio/Fs RPCs stubbed. Direct tail calls bypass the function table (hooks only catch jal/jalr). Status: handled.

---

## 2. Open issues, critical first

### ISS-100 Freeze after the match loads (RAM-wide lane-3 bit-31 corruption)  [CRITICAL]
- Symptom: after the 14 MB motion-data load (memalign 0xD60000 -> `007B7240`) and PAC resource load (archObj `016B3920`, hdr `016B3940`), bit 31 gets set in word 3 of every 16-byte quadword across all of RAM. Examples: scene pointer `0x39A6DC` `00738220` -> `80738220`; mainCb `801BFC70`. The main loop dies.
- Cause: UNKNOWN. A 128-bit store loop (MMI/`sq`/COP2 path) or a DMA/VIF unpack writing a wrong lane are the leading suspects.
- Instrumentation: `[canary]` block in `startScenePoll` (`svr06_overrides.cpp`) polls every 300 us; when `(rd(0x31618C)|rd(0x39A6DC)|rd(0x3B5E7C)) & 0x80000000` it prints all-thread snapshots (pc/ra/entry/sp) and runs three RAM scans (count/first/last offender, 40 ms apart). Not fired yet in the runs so far.
- Next: when it fires, take the first/last offending addresses + thread pcs; look for the loop writing that range; check VIF1/VU1 unpack paths and MMI macros (PEXT*/PCPY*/POR/PINTH) used by memset/memcpy-like code. See `findings/analyses.md` A-02.

### ISS-101 Loading/tip screens covered by orange/yellow/green streaks (garbage camera matrix)  [CRITICAL for visuals]
- Symptom: 3D scene drawn with garbage view/bone matrices during loading (PCSX2 shows the proper tip screen). Also the "SVR logo wipe covers the demo screen" artifact.
- Findings: VU1 data snapshots show garbage in bone matrices (qword 0x321, stride 4) and view matrix (0x3f4-0x3f7). VU1 program at pc 0 (skinned transform), 0xe60 and 0x22b0 other transform/clip programs.
- Evidence (24 VU1 data snapshots during one 20-tick window on a loading/tip frame, 2026-10-27): qwords `0x3f4..0x3f7` are IDENTICAL in all 24 snapshots (never refreshed) and contain floats that are exact integers times 2^32 (e.g. 7500.0*2^32, -21728.0*2^32; bit 28 set in many words) plus `0,0,0,-47630.36`; qword `0x321` (first bone matrix) goes from random finite values to an all-NaN pattern (`ffffffff 7fffffff ffffffff ffffffff`); the constants at `0x3f8/0x3f9` (0.16,0.16,0.2,1.0 / 0.35,0.35,0.35,0) look sane. In the GS stream the 3D scene's TSTRIPs collapse to a single point (bbox x 208, y -1522) and the HUD pieces (see ISS-103) get coordinates in about -1600..+2100 px. Together these explain the streaks, the missing pill/HUD pieces and the logo wipe.
- Instrumentation: `[vifmat]` (CL-0027, `ps2_vif1_interpreter.cpp`) logs every VIF1 UNPACK that touches VU1 qwords `0x3f4..0x3f7` (format, vuAddr, num, cl/wl, guest source address, first 16 bytes), first 200 occurrences; use it to find the EE packet that carries the matrix and then find who built it.
- Hypotheses: (a) uninitialised/garbage source data in RAM (possibly the same corruption as ISS-100), (b) EE-side VU0/COP2 math differences: `VDIV` by zero gives 0 instead of +-FLT_MAX, `VRSQRT` ignores `fs` (computes 1/sqrt(ft) instead of fs/sqrt(ft)), `VSQRT` clamps negatives to 0 instead of using |ft|, no flag side effects. See ISS-102 and `findings/analyses.md` A-03/A-06.
- Next: trace the writer of the matrix source in RAM; compare the first matrix upload with PCSX2.

### ISS-102 VU0 (COP2 macro) divide/sqrt semantics differ from hardware  [HIGH]
- Where: `ps2xRecomp/src/lib/vu_translation_helpers.cpp` (`translateVU_VDIV/VSQRT/VRSQRT`); generated code embeds the expression, so regeneration of the affected functions is required. Header macros `PS2_VDIV/PS2_VSQRT/PS2_VRSQRT` in `ps2_runtime_macros.h` are unused by the translator and also lack clamping.
- Hardware behaviour (per PCSX2 `_vuDIV/_vuSQRT/_vuRSQRT`): divide by zero (or denormal) -> Q = ((fs^ft) & 0x80000000) | 0x7F7FFFFF (and D/I flag set); results with exponent 0xFF clamp to +-0x7F7FFFFF; exponent 0 flushed to +-0; `SQRT: Q = sqrt(|ft|)`; `RSQRT: Q = fs / sqrt(|ft|)`, ft==0 -> sign(fs)|0x7F7FFFFF. ADD/SUB/MUL already go through `ps2_vfinish/ps2_vslow` and are fine.
- Status: FIXED-UNVERIFIED once implemented (planned CL-0030); needs regeneration of functions containing COP2 DIV/SQRT/RSQRT.

### ISS-103 Glyph "halftone box" around every text glyph  [HIGH]
- Symptom: every text glyph (title "Press START button", tip screens) has a halftone box around it in the native build; PCSX2 draws clean text.
- Facts established (2026-10-27): glyph sprites use `PSM 36 (T4HL)`, texture base `tbp0=0x1800`, `tbw=4`, 16-entry CT32 CLUT at `cbp=0x1810` (CSM1, csa 0), TEST `0x5000d` (ATE, ATST=GREATER, AREF 0, AFAIL KEEP, ZTE, ZTST GEQUAL), ABE with ALPHA `0x44`. The VRAM contents are clean (index 0 dominates, 55,912 of 65,536 texels); the palette is a white alpha ramp 0x00..0x80 over 16 entries, index 0 alpha 0, two entries black with alpha (shadow/outline). So the data is right; the box has to come from sampling / alpha test / blend.
- Code read (`gs_cpu_backend.cpp`): `passesAlphaTest`, `classifyAlphaTest`, `LookupCLUT`, `LoadClutUnlocked` (CSM1 swizzle), `combineTexture`, bilinear path and `WritePixel` (alpha test, DATE, Z test, blend) all look correct on inspection.
- Verified at run time (2026-10-27, `[glyphdbg]` over a 20-tick GS dump window on a loading/tip frame): the first sampled glyph texel had TEX0 `tbp0 0x1800 tbw 4 cbp 0x1810 tcc 1 tfx 0`, TEST `0x5000d`, ALPHA `0x44`, and `m_clut` entry 0 = alpha 0, entry 1 = alpha 0x0c, entry 15 = alpha 0x80 (correct); no sample had index 0 with non-zero alpha, and no sample differed between the cached read and a direct VRAM read. So texture data, CLUT load and sampling are CORRECT in that window.
- Conclusion: the glyph code is not the cause. The boxes/missing pill/HUD belong to the garbage-matrix family (ISS-101, A-03): the HUD pieces that share the glyph pass state (ALPHA `0x44`, TEST `0x5000d`) are drawn with wildly out-of-range vertex positions just before the glyph sprites (T8 512x64 `tbp 16000`, T8 128x128 `tbp 15936`, T4 64x16 `tbp 16128`; bbox about -1600..+2100 px). Re-check the glyph boxes after ISS-101 is fixed; if they persist, diff the glyph pass pixel by pixel against PCSX2.
- Status: INVESTIGATING (likely a symptom of ISS-101).

### ISS-104 Blue/red "Press START" pill bar and demo HUD banner missing  [HIGH]
- Symptom: title shows only text; demo lacks the bottom banner/HUD band (only faint "Press Any button"). Same family as ISS-103 (paletted/alpha draws).
- Next: dump the draws for those elements, compare PRIM/TEST/ALPHA/FRAME/TEX0 with PCSX2 (gifdump3.py).
- Update 2026-11-03 (CL-0111): title pill bar cause found: the game draws it, but the native movie compositor added the game picture onto the held title movie (BLEND_ADDITIVE), so low-alpha UI pixels vanished. Now luma-keyed over the movie. Status: FIXED-UNVERIFIED (title). Demo HUD banner still to check.
- Update 2026-11-03 (CL-0113/0115, verified in run d_*): the game draws the pill only after the title movie ends; our hold showed the movie's final black frame, so the pill sat on black. Now the brightest recent movie frame is held: logo + stadium + pill, matching PCSX2. Remaining difference: on hardware the background behind the pill is the game's own animated IPU stadium loop (ISS-122), ours is a still frame.

### ISS-105 Cage centre panels drawn as washed-out bands  [MEDIUM]
- Probable alpha-blend or Z/alpha-test difference. Not analysed yet.

### ISS-106 Steady 60 FPS  [HIGH, user requirement]
- Native is 52-59 FPS in places. Costs: VU1 interpreter runs synchronously (`vu1run` about 2 s per 3 s sample), software GS on the main thread; loading and memory-card loads drop frames.
- Remedy plan: move memory-card/loader work off the presenting thread; make VU1 and GS asynchronous; later a Vulkan backend (software GS kept as reference).
- Update 2026-11-03 (CL-0112): on Vulkan the emulated vblank is 60 Hz and the EE is mostly idle; intros/title/movies/memory-card screens ran 40-48 FPS because black frames triggered 2-3 synchronous GPU scanouts per vsync (black-frame DISPFB recovery) plus debug I/O (32 MB RAM dumps at fixed vblanks, per-frame vramw log). Fixed in CL-0112; `[fps60]` telemetry added. Remaining suspects: `wait_idle` per scanout, CPU box filter of the 2560-wide scanout, per-frame VRAM save sync (ISS-122).
- Update 2026-11-03 (CL-0114): `wait_idle` was the cost (15-22 ms per frame). Replaced by a fenced readback ring with the wait and downscale outside the GS lock: present 1-2.5 ms, intros/title/memory card/loading locked at 60 FPS without added input lag. Next: measure menus and matches (VU1 load).

### ISS-107 Window drag/resize pauses the game  [HIGH, user requirement]
- Windows modal move/size loop blocks the raylib thread. Remedy: run emulation and presentation on separate threads (the game thread must never wait for the window message loop); keep presenting last frame while dragging.

### ISS-108 Loading screen shows junk rows; single Start press on Press START then idle demo; doubled music  [MEDIUM]
- Not investigated since 2026-10-25. See `status/requirements-and-plan.md`.

### ISS-109 Press START menu bug  [MEDIUM]
- First Press-START screen missing textures, second correct; pressing Start on the first doubles the music. Probably the title scene initialised twice.

### ISS-110 Memory card round trip, Unlock All, XInput, widescreen/4:3  [MEDIUM]
- HLE `sceMc*` exists (`Kernel/Stubs/MemoryCard.cpp`); create/save/load not yet verified. Unlock All needs an Options entry (user: memory card first, then Options -> Unlock All).

### ISS-117 VIF1 UNPACK V4-5 does not expand the 5-bit channels  [MEDIUM]
- Where: `ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp`, UNPACK branch `vl == 3 && vn == 3`. It stores `packed & 0x1F`, `(packed>>5)&0x1F`, `(packed>>10)&0x1F`, `(packed>>15)&1` unshifted.
- Hardware (and PCSX2): R,G,B = 5-bit value `<< 3`, A = bit 15 `<< 7` (0x80).
- Effect: vertex colours sent through V4-5 (if used by the game's UI/model packets) come out nearly black.
- Related unchecked behaviour in the same function: for V2/V3 formats the interpreter keeps the OLD destination lanes for the components that the format does not supply (hardware writes defined values; verify W handling for V3-xx and Z/W for V2-xx against PCSX2 `VIFunpack`).
- Status: OPEN, fix planned as CL-0028 (runtime source, no regeneration needed).

### ISS-111 `[sema] spin` hack fires falsely under `SVR06_TURBO=1`  [LOW]
- `EeScheduler::signalSemaphore` "broke i-signal spin" logic; the kick loops at `0x1C0B00 / 0x113F70` are normal. Remove or scale by frames.

### ISS-112 Old bank D (`svr06_ovl_demo.cpp`, window 0x4D0000-0x59E67C) duplicates `ovxGame`  [LOW]
- Retire once `ovxGame` is proven.

### ISS-113 `PMTHL.LW` ignores pipeline 1  [LOW]  (see ISS-003)

### ISS-114 Debug instrumentation must be removed before shipping  [LOW]
- List: `[vudump]` in `ps2_vu1_core.cpp`; `[canary]`, `[addrinfo]`/free history, `[gabig]`, `[gaalloc]`, `[livereport]`, `gaWatch`, `resLoadWatch`, `heapAllocWatch/heapFreeWatch` (`SVR06_NOHEAPCHK=1` disables), `[scenepoll]`, `[bankprobe]`, `cdReadTimed`, `[sema] spin`, `[prof]/[tprof]`, `[glyphdbg]`, `[texlog]`, `[clutcmp]`, `[vramdump]`, `[cyan]`; `run.cmd` env (`SVR06_GSDUMPTICK/LEN`, `SVR06_TRACE`, `SVR06_TURBO`, `SVR06_MAXSECS`). The run log is 5-6 MB per minute with these on.

### ISS-115 Game screenshot capture returns black images  [LOW]

### ISS-116 `analysis` PCSX2-vs-native comparison items still to document pixel-exact  [LOW]
- After the above are fixed, re-run the 11-screenshot comparison and record residual differences.

---

## 3. Quick lookup: symptom -> first thing to check
| Symptom | Check first |
|---|---|
| `[guest-branch:missing-target] ... target=0x...` | Is the target a function start? Look in `register_functions.cpp`; check overlay banks for a missing `case` label (ISS-006/007/008). `[addrinfo]` prints live/freed heap block info (dangling object). |
| Thread Dormant (`st=5`) | The thread died on a missing-target or a NULL call. Read the line above it in the log. |
| `[watchdog] STUCK? running=0` | Normal per-frame state, not a hang. |
| Loading hangs in double-precision math | PMFHL/PMTHL macros (ISS-003). |
| A wait loop on a hardware register never exits | 32- vs 64-bit compare (ISS-005). |
| `new` returns NULL for tiny sizes | Heap limit / arena 15 full (ISS-009). |
| Wrong function runs after a jal into a leaf | Missing entry/label (ISS-006/007/008). |
| Texts get boxes / palettes wrong | ISS-103; check CLUT load (CLD) and T4HL path. |
| Garbage geometry streaks | ISS-101/102/100. |
| Build fails with "Permission denied" | Another build is running; kill ninja/cmake and delete `.ninja_log.restat`. |
| Edits to runner copies disappear | `fast_rebuild.ps1` overwrites from `overrides\` (ISS-014). |

## ISS-121 (2026-10-29) intermittent black boot
- Symptom: one run (23:27 UTC, build with CL-0075/0076 + collresp log) stayed black for 5+ minutes: shots all 0.0 brightness, 15900+ frames, zero `[movie]` log lines, no Vulkan errors; relaunch of the same build played movies normally.
- Evidence: syscall sequence differs from a good run only by timing (thread wake order near `[sys] #3..#11`), so it is a startup race (movie/thread init order). Not yet root-caused; happens rarely. Watch for recurrence before investigating (log: `prev_*_run_err.txt` with `[movie]` count 0).

## ISS-122 (2026-10-29) Vulkan build: 2D UI overlay missing on title and menu (user report + window screenshots)
- Reference (PCSX2): title shows "Press START button" pill bar over the logo video; main menu items over the V-wipe video; ONE Start press advances; title music keeps playing through the transition.
- Pre-Vulkan renderer (user screenshots): bar present (with box artifacts around the spaces in the text); the earlier two-Start-press / music-restart problem was attributed by the user to the first screen being treated as background video because the button overlay was not drawn on it.
- Current Vulkan build (window screenshots): title logo video shown WITHOUT the pill bar; after Start the V-wipe menu video shows with NO menu items; V-wipe video edges are combed (alternate rows shifted) and the "R" of RAW looks mirrored.
- Facts gathered: PMODE=0x8007 (both circuits enabled, ALP 0x80), DISPFB1==DISPFB2 always (runtime writes both the same), ping-pong between DISPFB 0x1400/0x1450; the bridge has a "DISPFB fallback" that forces both DISPFB to the last FRAME buffer when DISPFB points at an undrawn buffer; game draw FRAMEs seen: FBP 0x00 and FBP 0x50 (FBW 10), plus FBP 0x1080/0x1000 loading screens.
- Open hypotheses: (1) UI sprites use PSMT4HL/CLUT textures that the Vulkan path draws wrong or not at all; (2) the UI is drawn into the other double-buffer than the one scanned out; (3) video is drawn by IPU+GS in a separate pass that overwrites the UI. Next: RUN_GSDUMP capture of the title frame and replay in paraLLEl-GS/PCSX2 dump viewer; log per-frame draw counts per FBP vs displayed FBP.

### ISS-122 update (2026-10-29 late) - title overlay investigation, tooling + findings
- New diagnostics (gs_vulkan_bridge.cpp, backups .bak_flagdump/.bak_gifcap/.bak_gifcap2): create `analysis\vk_dump_now.flag` (file content = number of vsyncs, default 1) in the game's cwd -> writes `analysis\vk_vram_now_N.bin` (VRAM readback; NOTE GPU-rendered framebuffers are NOT in it, only uploaded data) and `analysis\vk_gif_N.bin` (raw GIF records: u32 size,u32 path,data; vsync boundary marker = 0xFFFFFFFF,0xF00D). Parser: analysis\gifparse.py / gifparse2.py (build_svr06b_clang\analysis).
- run.cmd no longer overwrites SVR06_GSDUMPTICK/LEN if already set (.bak_gsdump); RUN_LOGGED.cmd reads `analysis\gsdump.cfg`. The old gs_frontend GS dump does NOT fire in Vulkan mode (svr06MtgsSubmit returns early) - use the flag capture instead.
- Findings at the title screen (PCSX2 reference has logo + "Press START button" bar + text):
  * Every vsync the Vulkan GS receives identical ~445 KB: 20 PSMT4 glyph sprites (prim 0x5E, TEX0 tbp0=0x3420 cbp=0x3424 cld=1, alpha 0x44, test 0x5000d), 256 video macroblock uploads (16x16 PSMCT32 to dbp 0x1e00) and a 256x32 + 256x128 font atlas/CLUT upload (done AFTER the glyph draws, so glyphs sample last frame's atlas).
  * NO draw in the stream for the bar, logo or video quad; no FRAME_1/XYOFFSET/SCISSOR inside the frame (set earlier / via writeReg). Draw target FRAME_1 = FBP 0 (fbw 10) while DISPFB1/2 = 0x1450 (FBP 0x50) stays constant -> glyphs go to a buffer that is not scanned out; the visible picture is the bridge's DISPFB fallback scanout of the video buffer.
  * Hypothesis: the bar/logo are drawn into FBP 0x50 or the draw/display buffers are meant to alternate (sceGsSwapDBuffDc HLE sets DISPFB but the game's FRAME_1 flip is only in the register path); next step: log writeReg FRAME_1/DISPFB writes per vsync and compare with PCSX2 (GS dump at the same screen).
- ISS-122 capture #2 (8 vsyncs, title, incl. direct register writes): frames alternate FRAME_1 FBP 0x50 (even) / FBP 0 (odd) with XYOFFSET y half-line jitter (field mode 640x256); each frame = HLE draw-env writes + a 640x256 black clear sprite + 20 glyph sprites + font atlas re-upload (256x32 CLUT-ish at 0x3bd0, 256x128 at 0x3420) + 256 video macroblock uploads (16x16 PSMCT32 -> dbp 0x1e00 = page 240). DISPFB1=DISPFB2=0x1450 constant. NO textured video quad, logo or bar draw exists in any frame. VU1/XGKICK is not involved at the title (only in 3D scenes).
- Conclusion: the displayed picture is only produced by presentFrame's DISPFB fallback scanout of the video buffer (page 240), so the game's own composited output (clear + glyphs on FBP 0/0x50) is never what is shown and the glyph text/bar never appears. Real HW/PCSX2 must show page 240 through the second CRT circuit (DISPFB2 = video buffer, PMODE EN1+EN2 alpha blend over the glyph buffer on circuit 1) - our HLE forces DISPFB2 = DISPFB1. Next batch (single rebuild): make the movie/video player's DISPFB2 write reach p.dispfb2 (or synthesize circuit 2 = last MB-upload buffer while a movie is active), keep circuit 1 = game buffer, rely on paraLLEl-GS PMODE blend; then re-capture to verify the glyphs appear.
- ISS-122 capture #3 (title, 4 vsyncs, rebuilt capture incl. uploadImage hook): DISPFB1/2 toggle 0x1400/0x1450 normally (earlier 'constant' reading was a snapshot). The 256 video macroblocks per vsync are ONE BITBLTBUF (dbp 0x1e00 dbw 4, 16x16) followed by 255 TRXPOS/TRXDIR+IMAGE pairs = a 256x256 video texture at page 240; TEX0 tbp0=0x1e00 is set once per frame. The only PRIM draws per frame are the 20 PSMT4 glyph sprites (prim 0x5E, tex 0x3420) - there is NO sprite that samples the 0x1e00 video texture and no bar draw, and gsvk::uploadImage is never used at the title (all via gifTransfer). The first prim appears after all MB uploads in the frame (event index 800 of 1731).
- So the video quad + bar draws are being lost BEFORE gsvk::gifTransfer (candidates: PATH3 masked FIFO m_path3MaskedFifo never flushed (ps2_memory.cpp 2121-2128, VIF1 MSKPATH3 at ps2_vif1_interpreter.cpp 393), arbiter queue not drained for Path1/Path3 submissions that use drainImmediately=false, or a DMA ch2 chain/VIF1 DIRECT parse that stops early). Next batch (single rebuild): log per-vsync sizes/counts of every submitGifPacket by path, m_path3Masked state and queue/FIFO depth at presentFrame; fix whichever holds the data.

### ISS-122 update (2026-10-30 early) - PCSX2 reference GS dump of the title (snaps\...20261029231233.gs.zst)
- Dump layout: header 0x2e + 640x480 screenshot (0x12c000), state size at hdr+0xc (4194813), then 8192 B priv regs, packet stream starts 0x52e233 (type0=transfer[path,u32 size,data], 1=vsync, 3=regs).
- PCSX2 title frame (1 vsync, 272 packets): 640x256-field clear (PRIM 6) -> TEXTURED QUAD (prim 0x5e, TEX0 tbp=0x1e00 tbw=10 psm0 1024x512, x 1728..2368 y 1923.4..2179.4 = 640x256, RGBA 7f7f7f7f) -> full 640x448 PSMCT32 video frame upload to 0x1e00 (40 x 1792 qw) -> THREE bar draws (tristrip prim 0x5c, TEX0 tbp 3be4 x2 (end caps) and 3be0 (centre) psm20=PSMT4 CLUT, x 1863..2233 y 2104.6..2124) -> font atlas uploads 0x3bd0 (2048 qw) + 0x3420 (8192 qw) -> 20 glyph sprites (prim 0x5e, tex 0x3420).
- Ours (capture #2/#3): clear + 256 MB/vsync video uploads + atlas uploads + the same 20 glyphs. MISSING: the video quad and the 3 bar draws (bar atlas is part of the 0x3bd0 upload, so the data IS present). Hence picture = fallback scanout of page 0x1e00.
- Conclusion: the quad + bar are issued by game code between the clear and the atlas upload; in our run they are never issued (not a bridge/arbiter drop). In PCSX2 the whole 1120-MB frame decodes within one vsync; ours decodes 256 MB/vsync (IPU throttle) so the game's per-frame 'frame ready' path likely never fires. [ipudma] log shows endless 0x30-qw ping-pong feeds (0x1800/0x3300). Next: find the caller that emits the prim 0x5e quad (search EE code for sprite GIF tag w/ TEX0 tbp 0x1e00) and the frame-ready flag; compare with IPU completion/timing (IPU_CTRL/FDEC/IDEC done, DMA ch3 tag handling).
- ISS-122 addendum (2026-10-30): the 0x33EDA0 GIF-tag template is a generic quad template used by the 2D object methods at 0x1B80F0 (text/glyph renderer; loops chars via 0x111350), 0x1B8830, 0x1B8B00 - they RUN in our build (SVR06_TRACE TtlA/TtlB/TtlC, thousands of calls) so they are not the missing quad/bar. KEY DIFFERENCE: our title video upload is BITBLTBUF dbp 0x1e00 **dbw=4**, 16x16 per MB (256 MB/vsync, 256x256), whereas PCSX2 uploads a **640-wide (dbw=10) 640x448 frame** in 40 big IMAGE chunks and draws it with a textured quad (tex 1024x512). => in our run the game is on a different video path/state than real hardware (title movie is host-played via svr06_movie_start for ptid 15 idx 3; the game's own IPU stream is the 256x256 one). Real title = game-decoded background video + bar + text all at once; ours = host movie phase, then the real title only after a second Start (matches the user's 'two Start presses' report).
- Plan (batched into next rebuild): (1) let the game's own IPU/libsfd path handle the title movie (stop host-playing ptid 15 idx 3, keep BGM1 start), verify IPU picture size 640x448 / dbw 10 and that the quad draw appears; (2) intro vertical centering for frame-mode content; (3) menu screens. Diagnostic: run.cmd SVR06_TRACE now includes 1b80f0/1b8830/1b8b00 (backup run.cmd.bak_trace).

### ISS-122 update (2026-10-30 ~00:15 CT) - key result: HUD/title PSMT4 sprites ARE issued, they just don't show
- Attract demo (cage match) GIF capture: ~580 PSMT4 sprite draws (TEX0 tbp 0x3dc0 psm 20 cld=1, cbp 0x3dc4), ZBUF set, TEST/ALPHA identical to PCSX2 - yet no HUD on screen (only a stray flat green rectangle = HUD bar drawn with a flat texture). Title: 20 glyph sprites same story. => not a game-logic/state problem; the renderer (paraLLEl-GS on AMD RX 6700) is not showing 4-bit CLUT sprites whose atlas+CLUT were uploaded in the same frame.
- Hypotheses being tested: (1) sampler-feedback feature on the driver (SVR06_VK_NOFEEDBACK=1 -> DebugMode.disable_sampler_feedback, default ON in run.cmd for the test), (2) CLUT-load/upload ordering, (3) TEX0 CLD handling.
- Also: the host-played title movie remains (SVR06_GAMETITLE=0); letting the game decode it itself changed nothing (still dbw=4 16x16 MB uploads, no quad/bar), so the 256-wide uploads are what the game does in our environment (IPU path), not a host-movie artefact. Video quad + bar were never in our stream at the title, but HUD sprites in the demo are -> two separate causes.
- Code: gs_frontend.cpp vertical centring for 512-wide frame-mode screens (backup .bak_vcenter); gs_vulkan_bridge.cpp diag env switches (SVR06_VK_NOFEEDBACK / SVR06_VK_NOMIP, backup .bak_dbgmode); svr06_overrides.cpp SVR06_GAMETITLE switch (backup .bak_gametitle); run.cmd trace + env defaults (backup .bak_trace).

## ISS-122 addendum 2026-10-30 (late): title/HUD text invisible = per-frame atlas upload carries ZERO payload
Findings (all verified with runtime diagnostics, builds CL-0078 diag switches):
- Our title-phase GIF capture uploads two textures every frame through VIF1 path 2: dbp 0x3bd0 (2048 qw) and dbp 0x3420 (8192 qw = 256x128 CT32). In PCSX2 the same uploads carry real data (22,247 non-zero px; CLUT block at (16..23,96..97) is a grey/alpha ramp). In ours **every payload qword is zero** (only the 7-qword header is valid).
- Source: DMA chain tags (VIF1 ch 0x10009000, id=3 REF) point at game RAM 0x62ACE0 (qwc 2055) and 0x64D640 (qwc 8199). Header = {tag, FLUSH 0x11000000, DIRECT 0x2006, GIFtag nloop 4, BITBLTBUF dbp 0x3420 dbw 4, TRXPOS 0, TRXREG 256x128, TRXDIR 0, IMAGE 0x2000|EOP}; payload at 0x64D6B0..0x66D6B0 stays zero for the entire run (live RAM dump analysis/dumps/atlas_title.bin).
- Per-frame zero upload therefore **overwrites VRAM 0x3420..0x3620 every frame**, wiping the font texture that the game loads once via sceGsExecLoadImage (LoadImage dbp=0x3420 w=256 h=512 psm=0x14 src=0x632E50, and dbp 0x3520 psm 0x13 src=0x642ED0; both sources non-zero in RAM). That is why glyph/HUD/bar PSMT4 sprites are issued yet invisible.
- The upload buffers are game-heap blocks (header written at vs~1407 by the texture-resource registration loop 0x118100 which calls sceGsSetDefLoadImage/ExecLoadImage via 0x1174E0/0x1175A0). Nothing copies pixel data into 0x64D6B0 afterwards: no write through the game's copy routine 0x119340 (hooked, [cpywatch]), no CdRead target, no non-zero poll hit.
- PCSX2 rows 96..127 of the atlas equal contiguous RAM starting at 0x64AED0 (resource #2 tail); rows 0..95 match no RAM region -> they are produced by something (CPU glyph composer or a VU/VIF step) that does not run or fails in our build.
Next: identify the producer of 0x64D6B0 payload (candidate: font/glyph composer in the 0x111xxx-0x119xxx UI text code, UTF-8 reader 0x1111C0, glyph loop inside 0x1B7F40 at 0x1B80F0), or as an interim fallback skip/ignore uploads whose payload is all zero for dbp 0x3420/0x3bd0 so the loaded font survives.
Diagnostics added (env SVR06_ZEROUP=1, default on in run.cmd): [zeroup]/[zeroup-dma] in ps2_memory.cpp, [zeroup-vk] in gs_vulkan_bridge.cpp, [atlaswatch] and [cpywatch] in svr06_overrides.cpp (SVR06_NOCPYWATCH=1 disables).

## ISS-122 root cause 2 / ISS-123 — dropped branch delay slot at function boundary (2026-10-30, late)
Symptom: after the StoreImage readback fix (atlas rows 16-95 matched PCSX2), glyph/HUD text stayed invisible: the draws used CLUT base CBP 0x3424 where PCSX2 uses 0x35a4, so palette entries were read from the texture's own pixels.
Cause: VRAM page allocator `sub_00118EA0` (CLUT placement) ends its scan loop with `b 0x11908C` at 0x118FFC whose delay slot `sq $v1,0xA0($sp)` (0x119000) is the first instruction of the *next* generated function. The generator emitted `ctx->pc=0x11908C; return;` and never executed the delay slot, so the stack slot [A0] (= word index*32) stayed 0 and CBP = base+bit (0x3420/0x3424) instead of base+word*32+bit (0x35a0/0x35a4).
Fix: emit the delay-slot store in the taken path of `sub_00118EA0_0x118ea0.cpp` (backup `.bak_dslot`). Same defect found and fixed in `sub_00303010_0x303010.cpp` (`bgez` at 0x30302C, delay `ld $ra,0($sp)` at 0x303030; backup `.bak_dslot`).
Audit: scanned every generated sub_* whose last listed instruction is a branch/jump with a non-NOP word after it in the ELF image — only these two main-ELF functions (overlay hits in the scan are false positives from the main-RAM dump).
TODO (generator): in PS2Recomp codegen, when a branch's delay slot address lies outside the function range, emit that instruction inline before the transfer (both taken and fall-through paths). Re-run the audit after regenerating.
Diag added: `[vmgr]`/`[bt]` hooks in svr06_overrides.cpp (backup `.bak_vmgr`) log the manager state and bitmap probes; remove once verified.

## ISS-122 update (2026-10-30, late): atlas save/re-upload findings
- 21B250 (resource-bank flush) runs 118100 (register/upload) then 1B67E0 (sceGsExecStoreImage wrapper, ra 21B370) per resource; first pass (#2) saves the correct atlas (nzpx 14327, uploads 3420 T4 / 35A4 CLUT / 3520 T8 / 35A0 CLUT).
- After #2 the game legitimately uploads a 512x512 T8 texture at 34E8 (TIM2 via 1177E0/1175A0/116BC0, caller 5A4804) that overlaps the saved region; the atlas is then re-uploaded every title frame from RAM 0x64D6B0 (IMAGE dbp 3420 CT32 131072 B) - this part works.
- Second save (#8) happens on leaving the title (after Start) and is not part of title rendering; "Now Loading" text is visible in the Vulkan build after the CLUT fix (ISS-123).
- Diagnostics added: [save] (0x1B67E0 hook), [imgw] GIF IMAGE watcher (SVR06_IMGW=1, gs_vulkan_bridge.cpp imgWatch). Remove when ISS-122 closes.
- Tooling note: FLAGDUMP files land in PS2Recomp/build_svr06b_clang/analysis (game cwd), not svr06-recomp/analysis. svrlive quit works; go.flag alone does not relaunch (use the Fast rebuild desktop shortcut).

## 2026-10-30 (late) session notes: presenter/scanout, video, probes, animation map
- Scanout experiment (follow FRAME buffer instead of DISPFB) was WRONG as a default: FRAME is the back buffer in properly double-buffered scenes, so it showed unfinished frames (black/flicker, "video not working"). Now opt-in only: env SVR06_VK_FOLLOW=1 (gs_vulkan_bridge.cpp, .bak_follow). Movies verified working again (logo, brand, title).
- Probe-framework lesson: in svr06_overrides.cpp `covThunk` the generic path runs the ORIGINAL function BEFORE the special-case blocks; a special block must never call g_covOrig again (my first [anim] block did, double-ran skeleton_init with clobbered registers -> alloc 0x6657E410 NULL -> main thread died -> "frozen" game). Probes see post-call register state for a0..a2 only if captured earlier (a0,a1,a2,a3_0,t0_0 are captured pre-call).
- Host hang watchdog added in gs_vulkan_bridge.cpp: prints `[gsvk-hang] ... pres stage/gif stage` when no frame is presented for 8 s (stage ids documented in source). Not triggered in the last run.
- Perf defaults: run.cmd no longer enables SVR06_ZEROUP / SVR06_IMGW / SPU2 raw dump (115 MB file) by default.
- Animation probes: [anim:post] on 0x1BEC50 (skeleton_init), 0x1E1E90 (motion bank ctor: names /MOT/CAGE, /MOT/MINI, /MOT/SLEP seen), 0x1AB7E0 (model attach), 0x1DFFE0; skeleton census from one run: 171 one-bone, 8 x 17-bone, 2 x 26-bone, 2 x 19-bone, plus 115-bone and 103-bone models. See project doc claude/animation-map-2026-10-30.md.
- Loading/tips screen still shows the tiled/mirrored arena (ISS-101, garbage view matrix) and 'Steel Cage Match' tip text ghosting; the demo match did not start within 17 min in this run (loading phase very long).

## 2026-10-30 (evening): live console v2 (CL-0078)
- `SvrConsole.cpp` now polls `analysis/live/cmd.txt` from a dedicated thread every 150 ms (started on first presenter poll), so commands run even if the presenter stalls. Button `press`/`seq` stepping stays per-frame under a mutex.
- New command `diag NAME on|off` (names: imgw, storeimg, save, anim, vmgr, bt, atlas, cpy) toggles bits in `g_svr06Diag`; loggers in svr06_overrides.cpp ([anim:post], [save], [bt], [vmgr], [cpywatch]) and gs_vulkan_bridge.cpp ([imgw]) now check the bits instead of being always-on / env-only. No rebuild needed to enable them.
- Usage: `python tools/svrlive.py raw 'diag anim on; ee 0xADDR 64'`. Backups: `.bak_thread`, `.bak_diag`.
- Pending: `hook ADDR` live-register logging, gifcap/readback commands, startup script.

## 2026-10-30 (afternoon): animation chain verified, console `threads` (CL-0079)
- New console command `threads` (EE thread table: id/state/pc/ra/entry) via `g_svr06ThreadDumpFn` registered from ps2_runtime.cpp (backup `ps2_runtime.cpp.bak_thr`).
- Diag bits now gate [anim:post] [save] [bt] [vmgr] [cpywatch] ([imgw] too) without rebuild; `svrlive.py raw 'diag NAME on'`.
- tools/posecheck.py verifies on a RAM dump: world = local x world[parent] and skin = invbind x world, exact (1e-6) on 11 live skeletons (see project doc animation-verified-invariants-2026-10-30). Skeleton object == model owner; +0xA0 local, +0xA4 world, +0x100 skin, +0x114 inverse bind.
- Static map of the Chara update call tree and the helper-bone solver (0x1BF070) / world update (0x1BF3E0) in project docs animation-map / chara-update-callgraph.
- Observed (not fixed, deferred by user): main EE thread dies when the demo advances to the next scene: th1 st=5 pc=ra=0x018CAF40 (an object address used as a function pointer: JALR through garbage), stack frames show callers 0x5242FC (Game overlay 0x5242B0 -> 0x523E10) and 0x242B9C. RAM dumps frozen_ram.bin / frozen_vram.bin kept in build_svr06b_clang/analysis/live.

### CL-0080 (2026-10-30) — hardware write-breakpoint tool + animation applier found
- **Tool**: console `watchw ADDR LEN` now arms CPU debug registers DR0-3 (8 bytes each, max 32 bytes) on every game thread (no page protection; the old page-protect trap hung the game because file reads into a read-only page fail/retry). Hits are logged to `analysis/live/pagew.txt` as `[hwbp] ... guest~ADDR` with host frames resolved to the nearest registered recompiled entry (approximate: label entries share the host function; use as a hint only). Cap 200 hits per arm. `tools/autowatch.py [secs] [bone]` polls the live game, finds a wrestler skeleton (>=80 bones) and arms bone-N local matrix automatically.
- **Result**: every write to a wrestler bone local matrix (bone 5, skeleton with 83 bones, locals base = skel+0xA0 deref, stride 0x40) came from **0x20E7B0 → 0x20D9B0** (return label 0x20EEF4 after `jal 0x20D9B0`; chain 0x10502C → 0x114560 trampolines).
- **0x20E7B0** = per-channel keyframe applier: args a0=owner, a1=channel state, a2, a3=dest; f12 = blend t. Switch on channel type byte `ch+0x174` (2,4,6,0xA,0xE...). For each channel it lerps key A (`ch+0x34/0x38/..`) and key B (`ch+0x94/0x98/..`): `v = A - t*(A-B)` -> stores to `ch+4/+8/..`, then applies axis rotations/translation via 0x10BDA0 / 0x10BE20 / 0x10BD20 / 0x10BF90 / 0x10C930 / 0x10C960 (VU0 trig microprograms). Helper 0x20E580 does the same for another channel family. Tail calls 0x20D9B0.
- **0x20D9B0(obj, euler)**: `node = obj+0x20`; builds rotation with 0x10CBE0 (euler->matrix) into `node+0x10` (the bone local 4x4), copies translation qword to `node+0x40`, then 0x1AC820 (skin/world update into scratchpad 0x70000100/0x140, multiplies by inverse-bind with VU0). So bone local matrices are generated each frame from interpolated Euler channels, not copied from precomputed matrices.
- Skeleton progress: 70% -> 75% (applier located; selection/caller of 0x20E7B0 via virtual dispatch and the keyframe fetch not yet mapped).
- Notes: demo freeze (th1 dead at 0x018CAF40) still deferred; one run froze at ~frame 28875 before wrestlers loaded.

### CL-0081 (2026-10-30) — engine task-tree and animation controller classes
- Engine objects are **tree nodes** built by ctor 0x104D80(node, FOURCC tag, parent): `node+0x00`=tag, `+0x04`=flag, `+0x08`=next sibling, `+0x0C`=first child/parent link, `+0x10`=owner, `+0x14`=1, **`+0x18`=vtable pointer**. 0x1050C0(node, FOURCC) = find node by tag in the tree (e.g. 'CHAR' = 0x43484152, 'MCTL' = 0x4D43544C). 0x105030 = default tick (vtable+0x18): walks children and calls each child's virtual method `vtable+0x18`.
- Vtable layout (base from node+0x18): +8 dtor 0x104EC0, +0xC 0x104F00, +0x10 / +0x14 class-specific, +0x18 tick.
- Animation controller class: vtable **0x37D9F0**: `+0x10 = 0x20E020` (calls 0x20E0F0/0x20E330 channel setup chain, 0x20D9A0 per-channel setters), `+0x14 = 0x20EC00` (**calls 0x20E580 and 0x20E7B0**, the keyframe appliers from CL-0080), `+0x18 = 0x105030`. Sibling class vtable 0x37DA10 (ctor 0x20DA10, `+0x10 = 0x213910`) binds fields `CHAR+0xB4/+0xB8/+0xBC` (0x20DA10 looks up 'CHAR' via 0x1050C0).
- Per-frame driver: the huge function **0x2387A0** (10.8k lines) builds/steps the character pipeline: constructors 0x20D910 (controller node via 0x20D8E0), 0x20DA10, 0x20EF20, 0x210140 are created in sequence with 0x119800/0x1050C0 and virtual calls (`jalr $t9`).
- Skeleton progress: 75% -> 80%.

### CL-0082 (2026-10-30) — tools/autowatch2.py
- `tools/autowatch2.py [secs] [n]`: waits for a wrestler skeleton (>=80 bones), finds controller/MoHo nodes (vtable 0x37D9F0 at +0x18, tag 'MoHo' at +0), arms `watchw` on node+0x40 (first pose matrix, 32 bytes). Not yet exercised: the last run sat on a black screen (no wrestlers/objects in RAM) after a forced Start; the MoHo nodes seen in an earlier dump (11, 21 matrices at +0x40, stride 0x40) are the next breakpoint target to find the keyframe fetch.

### CL-0083 (2026-10-30) — MoHo node pose matrices are static
- hw write-breakpoint on 'MoHo' node (vtable 0x37D9F0) +0x40..+0x60 (first pose matrix) saw **no writes** in 15 s of demo: those 21 matrices (stride 0x40, near-identity) are rest/reference data, not the per-frame output. Keyframe source/fetch remains unmapped (next: break on channel state `+0x34/+0x94` of the 0x20E7B0 arguments, capture a1 at the hit). Skeleton stays 80%.
- Verified ISS-102: ps2_vdiv/ps2_vsqrt/ps2_vrsqrt (ps2_runtime_macros.h) exist and are used by generated COP2 code in sub_0010C*; status stays FIXED-UNVERIFIED. Starting visual-artifact pass (ISS-101/103).

### CL-0084 (2026-10-30) — ISS-101 narrowed: loading/tip-screen garbage = bad translation lane of the view matrix
- Tools added: diag bit 8 `vifgarb` (ps2_vif1_interpreter.cpp: when the VIF1 UNPACK covering VU1 qwords 0x3f4..0x3f7 carries any |float|>=1e9 or NaN, log to `analysis/live/vifgarb.txt` and, if the source is in scratchpad, arm a hw write-bp there); console `watchs OFF LEN` (hw write-bp on EE scratchpad), `spr OFF LEN` (scratchpad hexdump), `threads` now prints prio/waitReason/waitId/susp/wake; `svr06HwWatchHost()` generalises the breakpoint to any host buffer.
- Observation (loading screen after the demo, reproducible every frame): uploaded matrix = rows `(0,0,0,w0) (0.597,1.0,1.0,w1) (0.802,~0,~0,w2) (0,0,0,1)`. The x/y/z lanes look like a valid (not orthonormal) rotation; the **w lanes are integers converted to float** (0xcb22a411 = -0xA2A411, 0xcb300002 = -0xB00002, 0x55a29c21 = 0xA29C21·2^k) — i.e. an uninitialised/integer-typed translation. In good demo scenes the w lane is a small translation (e.g. 4.578/55.308). The value is identical for all draws and all frames → the camera/scene object for this screen never gets a proper position (not a per-frame math glitch).
- Delivery path: the packet is copied by the DMA HLE (`sceDmaSend` 0x11DD90 ← 0x1A9B24 (in 0x1A9A70) ← 0x1B4530 ← 0x1AF010 via the 0x114560 virtual-call trampoline) into display-list buffers at 0x1CF9ED0.. (stride ~0xF0), many copies; the VIF source pointer is neither RDRAM nor scratchpad (staging buffer in the DMA layer), so the original producer is upstream of 0x1AF010.
- Also verified: MoHo/ctl nodes absent on this screen; ISS-102 helpers present.
- Next: break on the 0x1AF010 caller to capture the camera/scene object feeding the matrix (ctor of the loading-screen scene), compare with PCSX2 title/tip dumps (`claude/iss-122-pcsx2-title-dump-2026-10-30.md`).

### CL-0085 (2026-10-30) — renderer matrix chain mapped; streaks also appear IN the demo
- Gfx context object (global pointer `gp-0x6AD0`, seen at 0x1CF9360): `+0x3B0` view matrix (= camera object `+0xE0..+0x11F`, copied by **0x1A8EF0(gfx, cam)**, which also stores the camera pointer at `+0x590`), `+0x3F0` second matrix, `+0x430` light/viewport constants, `+0x470 = (+0x3F0) x (+0x3B0)` computed by **0x1A8FF0** (VU0 vmulax/vmaddw chain; result w lanes = translation row), `+0x4B0` clip/screen set. Init of the context (identity via vmr32 of vf0) is 0x1A87B0; scene packet builder 0x1B45A0 copies `+0x470/+0x430/+0x370` into the VIF packet (UNPACK 0x6C0C03F4, 12 qwords) staged in scratchpad 0x70002000.
- Single camera object 0x62BDD0 (static data) is used for every scene in the demo; its matrix is sane there (translation row +0x110 changes smoothly per frame). `[camlog]` (camlog.txt, patch in sub_001A8EF0) logs callers: ra 0x2232A4, 0x2260FC, 0x302C1C, 0x3000EC, 0x300164, 0x520184, 0x226E94, 0x5228B0.
- The matrix at `+0x3F0` had rows 1 and 2 identical in a live dump (degenerate) — matches the A-03 "rows 1 and 2 equal" note; the loading-screen garbage is a different state of the same chain.
- NEW: screenshot at demo frame ~47.7k (vsync 11576) shows coloured streaks and a teal slab across the wrestler scene during the demo itself, while all EE skeleton matrices (local/world/skin/inv-bind) are finite and sane (checked on 14 skeletons) → remaining culprit is on the VU1/VIF/GS side (bone matrices upload at qword 0x321+, vertex unpack, or the VU1 skin program), not the EE animation chain. Next: capture a GS dump window at tick ~11500 (`scripts/RUN_GSDUMP.cmd`, set tick) and inspect the streak primitives with gifdump3.py.

### CL-0086 (2026-10-31) — gifcap console tool; ISS-101 root narrowed to the camera object's translation row
- Found: the old `SVR06_GSDUMPTICK` / `analysis/gsdump.cfg` GS dump is DEAD in the Vulkan build — `svr06MtgsSubmit` returns into `gsvk::gifTransfer` before `GS::processGIFPacket` (where the dump lived). Two full runs were wasted on this. `gsdump.cfg` also silently overrode RUN_GSDUMP's tick (backup `gsdump.cfg.bak_2600`).
- NEW tool: live console `gifcap N` (gs_frontend.cpp `g_svr06GifCap`, hook in svr06MtgsSubmit; SvrConsole.cpp) writes the next N raw GIF packets (u32 size + data, same format as the old dump) to `analysis/live/gifcap.bin`; parse with `tools/gifdump3.py`. Backups `.bak_gifcap`. Loading-screen capture saved as `analysis/gifcap_loading_1031.bin`.
- Loading-screen capture: 44,814 draws; almost every TSTRIP vertex sits at x=2047.9375,y=2079.9375 (the 12.4 fixed-point saturation 0x7FFF) or at (0,0), with garbage UVs (e.g. 463,883) → VU1 transform output saturates because the view matrix is garbage → the streaks. 
- Camera object 0x62BDD0 +0xE0 matrix on the loading screen: rotation rows plausible, but translation row (+0x110 = 0x62BEE0) = 0xCB232882, 0xCB280002, 0x55A32070 (= -1.07e7, -1.1e7, 2.2e13), w=1.0. `[camlog]` shows the bad matrix arriving at 0x1A8EF0 from callers ra 0x3000EC / 0x300164 (alternating), w lane -352116.
- hw-bp on 0x62BEE0 shows the periodic writers come from the 0x300xxx scene/camera module (host→guest map approximate: 0x30086C, 0x20EEF4, 0x227A50, 0x302CF4). Camera mode logic lives in the 6312-line function 0x2FF9F0 (camera preset table at 0x4A1AB0, stride 0x10, +0x100/+0x104/+0x108; table verified sane). Writer of the garbage translation still unidentified; hwbp handler now also logs the written value (`[hwbp] val`), backup `ps2_pagewatch.cpp.bak_val`.
- Demo hang note: after the demo the game reaches the loading screen with vsync advancing very slowly (frame counter stuck at 21012 while vsync crawled 20162→20285); threads all waiting (running=0).

### CL-0087 (2026-10-31) — first demo-streak capture (gifcap) + scan tools
- `tools/autocap.sh T N` polls vsync via the live console and arms `gifcap N` when vsync >= T (demo streak frames ≈ vsync 11300–11600, ~15 min after launch in this build). `tools/streakscan.py` / `streakscan2.py <gifcap.bin>` list suspicious wide/ribbon triangles via gifdump3.
- `analysis/gifcap_demo_1031.bin` = 4000 packets at vsync 11310 (22,077 draws). Screenshot at the same moment (`analysis/shots/vk_00037.ppm`) shows big yellow-green diagonal ribbons, teal slabs and striped panels over the cage match while both wrestlers and the referee render correctly → artifact is NOT skeleton/skin of the wrestlers.
- Scan result: ~74 ribbon candidates (triangle with 1–2 vertices on screen, others far off) targeting FRAME fbp=0 with T8 textures tbp 10496, 10624, 10272, 12060–12160, 13072 and flat per-triangle colours (e.g. rgba 0x80454136). Most plain degenerate strip padding at (0,4096)/(0,0) is a parser artefact (ADC/XYZ3 not modelled) and was excluded. Not yet matched to the visible yellow-green ribbons; next step is a draw-bisect (skip draws by TBP live) to identify which batch produces them.

### CL-0088 (2026-10-31) — pose investigation: euler->matrix proven exact; motion format = HCTP/Yukes (user's Tool program has a decoder)
- `[eulerlog]` (sub_0010CBE0, analysis/live/eulerlog.txt, backup `.bak_eul`): 800 live calls; output matrix == Rx*Ry*Rz (row-vector) with max error 0.0 on 224 non-trivial calls; sin/cos intermediates (v40/v50) correct. => VU0 micro trig + euler->matrix is NOT the cause of the bent/sitting pose; the angles fed in (keyframe decode or motion data) are the suspects.
- `[kflog]` (sub_0020E7B0, analysis/live/kflog.txt, `.bak_kflog`). Channel struct = 0x180 bytes (array at owner+0x20, count owner+0x24): +0x00 cur vec(x,y,z,w=1), +0x30 key A vec, +0x90 key B vec (B := previous cur when a new key is loaded), +0x160 translation, +0x174 type byte, +0x175 flag, +0x17C.. raw pose-record bytes. (My first kflog read was offset by one float; B==0 is normal for freshly created channels.)
- Key loader = 0x20F940 (writer found by hw-bp at 0x94 of a channel): per mask bit i: `B[i]=cur[i]; A[i]=(s16)raw * 2*pi/65536` — constant at gp-0x7E88 = 9.5874e-05 verified in RAM; wrap constants +-pi / 2pi verified.
- Channel type table at ELF 0x342840 (vaddr 0x342840) = bytes `7 7 7 14 23 6 7 3 14 6 7 3 14 7 2 14 10 7 2 14 10 7 7 7 7`, identical to the HCTP table documented in the user's `Tool program/Mod Maker Tools/hctp_motion.py` ("14 = y,z 8-bit (value<<8), 10 = y 8-bit when motion object mode 1; other modes widen 10->2, 14->6"). Channel ctor 0x20E330 copies this table into ch+0x174 and applies mode-dependent widening (mode at owner+0x704). Motion records live in PAC/M.PAC (54 MB, opened via sceCdSearchFile); ch+0x174 high bytes show flags (259=0x103, 279=0x117).
- Live pose dump `analysis/pose_dump_1031.txt` (tools/eulerdump.py) for the 111-bone wrestler at 0x18C2CF0: both knees strongly bent (left bone3 -87.5 deg about Y, right bone8 -49.7 deg), left thigh nearly horizontal => matches the "sitting/bent" report; skeleton math is correct so the angle data is wrong or mis-widened.
- NEXT (Skeleton): decode the same motion record with hctp_motion.py's reader (Y._rd_bone) from M.PAC and compare to channel A/B values (5 motion objects found in the RAM dump at 0x17ccc60, 0x17f4540, 0x182d190, 0x18cedf0, 0x1974b20 via type-sequence scan); verify the mode widening (owner+0x704) and the <<8 handling for types 14/10 in the pose-record parser (SvR06 equivalent of HCTP CMotionData::DataAnalysis 0x1b77d0 / SetBoneData 0x1b6bd0).
- Percentages: Skeleton 80 -> 82 (euler->matrix exactness, channel layout and key loader fully mapped, format identified). Others unchanged (Intro/title 97, Overall playable 76, Collision 50, Entrances 16, Vulkan 75).

## CL-0089 (2026-10-31) Ribbon investigation: real Vulkan VRAM dump + texture decode
- `vramdump` console command used to dump the dead software GS VRAM (stale); now calls `gsvk::dumpVram()` (map_vram_read of paraLLEl-GS local memory). Backups: gs_vulkan_bridge.cpp.bak_vramdump, SvrConsole.cpp.bak_vramdump.
- New tool tools/texdec.py (T8/T4 + CT32 CSM1 CLUT decode using the project's own swizzle tables).
- Ribbon-covering draws (ribbonmatch on gifcap_demo4 + shot_32): tbp 16128 (T4 64x16, cbp 16129), tbp 16000 (T8 512x64, cbp 16164): CLUT RGB = 0, alpha-only mask (black shadow/vignette-style overlays, prim 0x5c, alpha 0x44); tbp 10256 (T8 64x32, cbp 12456) = grey metallic streak texture, alpha 0x80, vertex colour 0x80xxxxxx.
- Conclusion so far: the yellow-green colour does NOT exist in the source textures/CLUTs (dump taken at vsync ~9.5k, pre-demo, so confirm in-demo). Colour is introduced on the Vulkan side (blend/ALPHA mode, FBA/PABE, or CLUT sampling) -> next: capture ALPHA/TEST/FBA state of those draws and compare against a PCSX2 frame; also re-dump VRAM at demo time.

## CL-0090 (2026-10-31) Ribbons are stretched-geometry, not colour/CLUT/blend
- Demo-time VRAM dump (analysis/vram_demo6_1031.bin, gifcap_demo6_1031.bin, shot_32) decoded with tools/texdec.py: wrestler face/body textures (tbp 13408 T8 128x128, 13304, 12064) and arena texture (10624) have correct pixels and palettes in Vulkan VRAM -> texture upload / CLUT path is CORRECT.
- The multicoloured smears in the demo frame are skin/trunk textures on hugely stretched triangles; the teal arena is ordinary arena texture under wrong transform. Earlier "yellow-green ribbons" = the same effect.
- Root cause is therefore geometry: bad skinning/bone matrices (pose bug, CL-0088) and/or bad view/camera matrix (ISS-101). Blend/ALPHA/FBA hypothesis dropped (CL-0089 follow-up superseded).
- Next: check skinned mesh vertex data for NaN/huge values vs bone matrices; fix pose (motion record decode) first.
- CL-0090 addendum: in the demo the camera object matrix (0x62BEC0..0x62BEEC) is sane (translation 11.9/21.4/68.4), so ISS-101 garbage is loading-screen-only. Stretched strips in demo frame = tbp 12064 (T8 64x64, prim 0x7c) vertices off-screen (y -210..-510): per-object skinning/bone matrices suspected. mirror flag (+0xC8) is 0 for 1194/1200 records (correct, not the bug). kflog type/t columns are mis-aligned (logger offsets), do not trust.

## CL-0091 (2026-10-31) PCSX2 ground truth, camera guard, gifskip bisect
- PCSX2 save state (sstates/SLES-53676 (0B05A5BD).01.p2s = zip, members zstd-compressed (method 93); extract with zstd CLI) -> eeMemory.bin saved as analysis/pcsx2_ee_0B05A5BD.bin. Tools: tools/moho.py (compares MoHo animation owners / channel type tables), tools/eulerdump.py works on it.
- RESULT: channel counts, type tables (7 7 7 14 23 6 ... / widened 23-variant) and modes (+0x700/+0x704) are IDENTICAL between PCSX2 and our RAM; cur/A/B channel semantics identical (incl. cur.z = -cur.x pattern on some channels). Real standing pose has knee euler ~-32 deg. Animation data path matches hardware; Skeleton 82 -> 84. The "sitting" look in earlier frames is not explained by key decode; re-check after camera guard.
- ISS-101 root: camera object at 0x62BDD0 is poison-filled (0x52525252) / zero between scenes; scene is still drawn with it -> saturated vertices. Fix: [camguard] in sub_001A8EF0 keeps last valid view matrix when |m|>=1e5/NaN (backup .bak_camguard). Camera in demo is sane (11.9, 21.4, 68.4).
- New debug: console `gifskip TBP` (-1 off) suppresses textured draws with TEX0.TBP (gs_frontend.cpp svr06GifSkipFilter, backup .bak_gifskip); tools/skiptest.sh, tools/bigtri.py (find huge triangles in a gifcap). `vramdump` = real Vulkan VRAM; tools/texdec.py.
- Ramp/entrance-light scene: remaining artifacts are intermittent full-screen teal overlay + yellow/blue radiating rays (user suspects the ramp light textures). Skipping tbp 12060 and 8192 did not remove them; rays look like light-cone geometry with vertices crossing the near plane -> suspect VU1 CLIP/FCAND path or the light-cone microcode; VU1 CLIP code reviewed (matches hardware semantics). Next: capture while rays are visible (game later freezes into a tiled static frame, GIF stops: known demo freeze) and run bigtri.py; compare with PCSX2 entrance dumps (snaps 233901-234143).

## CL-0092 (2026-10-31) Ribbons root-caused to the 2D demo-banner quad builder (vs PCSX2)
- PCSX2 run of the attract loop: title -> demo match (normal ring, letterbox + "Press Any button / DEMO" banner) -> "Steel Cage" loading tips -> cage demo -> title. No ramp/entrance scene; the camera at 0x62BDE0 (PCSX2) / 0x62BDD0 (ours, heap shifted -0x10) has the same view matrix in both (eye ~(-102,-38,-21) vs (-102,-39,-2)).
- PCSX2 savestate demo frame (analysis/pcsx2_demo_ee.bin, pcsx2_demo_vram.bin) vs ours: the banner GIF packet (TEX0 tbp 16000 T8 512x64 cbp 16164, tag 0x902E4000_00008001, regs 0x25252521) is byte-identical except the XYZ2 words: PCSX2 x=0x7000 y=0x89A4 (screen 0,410); ours 0x90C14100/0xDED31000 etc. => the "ribbons" are the banner/letterbox/button-icon quads with garbage X/Y.
- Writer: sub_001BA870 (2D quad builder): XYZ = cvt.w.s((float)int(*(gp-0x6AD0)+0x650/0x654) + a1->{x,y}) with unsigned fix-up; the cvt code is correct, so the input floats (a1 vertex array) or the offset ints are bad. [sprlog] added at its entry (backup .bak_sprlog) -> analysis/live/sprlog.txt.
- Tools: tools/pcsx2gs.py (PCSX2 .gs dump -> gifcap), tools/fxlist.py, tools/coverage.py, tools/findcam.py.

### CL-0092 follow-up (2026-10-31 21:55)
- [vlog] in sub_00114B50 ran through the whole demo: only obj=0x670640 / vt=0x37cbb0 / fn58=0x114aa0 seen, vec always sane (256 230 0 1). No "big" case, so the banner garbage does NOT come from the sub_00114B50 virtual-position path.
- Banner template at 0x537030 (tag 8001/902e4000/25252521) is overlay-shared RAM; after the demo it holds other overlay data, so it must be inspected DURING the demo (vsync ~10.9-11.4k).
- Next: log the ovlD vertex-float producer (the float array feeding cvt.w.s with scale 0x398DBC=1.1428571) during the demo; check chain-mode SPR DMA path; late in-game scene (f~48k) shows separate triangle-garbage (new issue to track).
- Debug logging ([vlog],[ftoilog],[camlog2],[nodelog],[dlog],[sprlog2],[bannerlog]) still in tree - remove before shipping (.bak_* files exist).

## Follow-up 2 (2026-10-31 evening) - beam quads, camera object 0x62C770
- The banner packets seen in live RAM (0x1d1aa30.. etc, tag 8001/902e4000/25252521) are written by the same call chain as the spotlight code: stack 0x214abc -> 0x21508c -> 0x1a9b24 -> 0x11dd90. XYZ words are int-ified floats of ~1e7 magnitude => the beam quad vertices come from the garbage view matrix; the 0x537030 'template' is a static overlay header, not the vertex source.
- [vlog] sub_00114B50: whole demo only ever sees obj 0x670640 (vec 256,230,0,1) -> NOT the source.
- Live: 0x4A1A30 translation = (-1.07e7,-1.1e7,2.2e13,1) while ribbons are on screen; a copy of the matrix block exists at 0x62C800 = camera object 0x62C770 (+0x90 matrix, translation +0xC0, +0xD0.. projection 1536/1953/2048). sub_00226030 (SetViewMatrix wrapper; 0x20A4D0 copies obj+0x90 -> 0x4A1A30) logged only sane obj 0x68a6c0 in the demo ([camlog3], 10 lines) => the garbage reaches 0x4A1A30 via the matrix push/pop at 0x302ABC/0x302B78 (save/restore 0x4A1A30/0x4A1A70 to obj+0x40/+0x80) or another copier, not via 0x226030.
- Writes to 0x4A1A60 / 0x62C830 during the ribbon scene are all garbage and slowly drifting (value changes ~1e-4 relative per frame) => it is a persistent value being re-copied, not freshly computed each frame. The first writer is still unknown.
- Values are exact integers as floats (-10658833, -11534338) => looks like an int->float or fixed-point leftover; compare with the PCSX2 dump to see which node/anim field the 0x62C770 camera takes its position from.
- Tooling: watchw+ramdump work together; svrlive.py lives in svr06-recomp/tools (run from svr06-recomp). 'watchw 0 0' (disarm) appeared to crash the game once.
- Debug aids still in tree: [camlog3] in sub_00226030 (.bak_cam3) plus those listed above.
- Next: log writes into the camera object 0x62C770 by finding its constructor/updater (class tag at +0xE0 'GDRF'?), hook the matrix save at 0x302ABC to log when |t|>1e5 is first stored, and diff against the PCSX2 object.

### CL-0092 follow-up 3 (2026-10-31 night): [camlog4] results
- sub_0020A4D0 (SetViewMatrix copy, ra 0x226064) is called every frame with a matrix whose translation is first sane (74.47,60.29,133.38), then flips to garbage in single frames (1.16e9,6.9e8,1.16e9), recovers, and later stays garbage (~1e9) => the camera node matrix itself (written by 0x20E7B0 node writer / camera mode code) gets transient garbage; the global 0x4A1A60 then also changes by itself afterwards (drifting -1.0719e7 -> -1.0738e7, +~6.2e3/frame) while the copy source holds -7e19 => something besides 0x20A4D0 keeps rewriting 0x4A1A30 (candidate: 0x302B78 restore path with a stale saved copy obj+0x40).
- Camera object address is not stable (0x68a700, 0x71e120 seen) => watch addresses must be re-derived per run from [camlog4] a0.
- hwbp frame mapping is unreliable (nearest registered function within 64MB); need a handler that prints the writer thread's R5900Context pc (not done yet).
- Debug aids in tree: [camlog3] sub_00226030, [camlog4] sub_0020A4D0 + sub_003028E0 (.bak_cam4).

### CL-0093 (2026-11-01) FIXED: generated VU0 code wrote to $vf0 -> garbage matrices / demo "ribbons" (ISS-101 family)
- Root cause: PS2Recomp-generated COP2 code emitted `ctx->vu0_vf[0] = blend(...)` for flag-only ops that target $vf0 (e.g. `vsub.xy $vf0,$vf29,$vf12` / `vsub.xy $vf0,$vf12,$vf30` in sub_00114F70 = RotTransPers clip test, plus sub_0010D3B0 / sub_0010D400). On hardware vf0 is hard-wired (0,0,0,1) and writes are discarded; ours kept clip-window deltas (~2079.99/2047.99) in vf0.xy. Every later `vmr32 ... $vf0` identity build (sub_0010C000 RotMatrix, CameraMatrix, ...) then produced rows with garbage lanes -> rotation/camera matrices with x/w junk -> view translation ~1e7..1e13 -> beam/banner quads at ~1e7 px.
- Evidence chain: [camlog5] showed sceVu0CameraMatrix input matrix sp+0xC0 (from sub_0010C000 RotMatrixX/Y on identity) holding 2079.99/2047.99 in its w column; hwbp+ctx-pc showed the writers 0x5205F8 (CameraMatrix) and 0x20A4F8.
- Fix: in the 3 generated files replaced `ctx->vu0_vf[0] = ` with a per-file thread_local dummy sink (`svr06_vf0_sink_<addr>`); backups `.bak_vf0`. The exact-transliterated lib (vu0exact_ops.h) already guarded vf0 (VFr/vfwrite). No other file in runner/ or overrides/ writes vf0 (scanned all 16750 + 9202 files).
- Result: demo scene (vsync ~10.7k) renders with no yellow/teal ribbons; ring ropes/wrestlers correct. Remaining in the same scene: noisy/teal textured block on the ring apron (bottom-left) = separate texture issue.
- TODO upstream: fix ps2xRecomp vu_translation_helpers.cpp to skip the destination write when the dest reg is 0 (so regeneration does not reintroduce it).
- Percentages: COP2 93 -> 96. Others unchanged.

### CL-0093 follow-up 2 (2026-10-31 night)
- Generator fix (vu_translator.cpp: vf0 writes redirected to a thread_local sink) is applied in source but NOT yet compiled/tested (ps2xRecomp not rebuilt).
- Demo scene is clean after the vf0 fix. The bottom-left "noisy teal/yellow patch" belongs to the post-demo scene (f~37k+), not the demo: gifcap there shows 3D strips (tbp 10252..10496, T8) with vertices far off-screen (e.g. 3902,507 / 3416,45) = garbage projection, same class as the earlier camera garbage; the noise block is likely an unpopulated/garbage-UV draw. Next: camlog5 on the post-demo camera call, check other vf0-writing functions (grep generated code for `vu0_vf[0] =` outside patched files).

### ISS-124 Bottom-left noisy teal/yellow HUD patch (2026-10-31, found via gifcap, demo + cage scene)  [MEDIUM]
- Draw: TSTRIP (2 verts, screen px ~18..170 x 335..465 incl. overflow), state ALPHA 0x44, TEST 0x5000d, TEX0 T8 128x128 tbp 15936 tbw 2, CLUT cbp 16160 (CT32, CSM1, tcc 1, tfx 0). Same pass draws the button-icon atlas (T8 512x64 tbp 16000, cbp 16164: decodes CORRECTLY - pad icons X/square/triangle/circle, START, SELECT) and T4 64x16 tbp 16128.
- Finding: VRAM at tbp 15936 holds 4-bit glyph-like data (bytes 0xE0/0x0E/0xC0/0x0A, one nibble used per byte, glyph cells visible but scrambled) and the CLUT at cbp 16160 decodes as junk (entry 0 = teal 0x4e8f8f00, entries repeat in 0x8f.. patterns). Index 0 therefore draws as a translucent teal rectangle, other indices as yellow/purple noise.
- No GS uploads (BITBLT/TRXREG) occur during steady demo frames, so the texture+CLUT are written once at load (or never). Hypotheses: (a) text/name-plate bitmap + palette built on the EE side and uploaded in a format we mis-handle (PSMT4/T4HL vs T8, or CLUT CSM1 layout), (b) upload dropped or aimed at a different DBP, (c) EE-side generator reads uninitialised/stub data.
- Next: capture the load-time upload (gifcap from boot or hw write watch on VRAM page 15936*64 words) and compare DBP/DPSM/size; dump EE source buffer.
- Tools: tools/texdec.py (T8/T4 decoder), ad-hoc CT32 decoder; gifcap + tools/gifdump3.py ORDERED GROUPS with XYOFFSET 1792,1792 (screen = vertex - 1792).

### ISS-124 update (2026-10-31 late)
- VRAM blocks 15936..16191 are a single 256x256 T8 HUD atlas (draws use sub-rects: 15936 128x128/256x256, 16000 512x64 band, 16064 128x256, 16128 T4). Right half of the band (pad icons) is correct; the rest holds left-over/scrambled data and a junk CLUT (cbp 16160 / 16196) -> teal/yellow patch.
- No uploads to this region in steady frames or in the sampled load windows; PCSX2 gameplay HUD frame never draws from these tbps (demo/attract-only banner). Hypotheses: render-to-texture aliasing (CT32 column at 15808 vs T8), ISS-122 StoreImage/restore, or a missed upload. Details: project doc claude/iss-124-hud-patch-2026-10-31.md. Helper scripts in analysis_tmp/ (fastscan2.py, scantex.py, hunt*.sh, watchvram.sh).

### ISS-124 update 2 (2026-10-31 22:15) - source identified
- The HUD atlas is NOT a one-off upload: the game re-uploads a 256x96 CT32 image from EE RAM 0x743FA0 (96 KB) to VRAM dbp 15808 (fbw 4) every frame (ISS-122 save/restore scheme). Blocks 15936..16191 = rows 32..95 of that image.
- The RAM buffer is filled by the StoreImage wrapper (guest 0x1B67E0, caller 0x1B69A8, via our sceGsExecStoreImage -> gsvk::readLocalToHostCT32). hwbp on 0x74BFA0 shows: host memset to 0, then readback writes (e.g. 30f00024 ffeefac0 fefffe21 0ddd11ff). So the scrambled atlas = what our Vulkan readback returned at save time.
- Tried: iface.flush() before map_vram_read in readLocalToHostCT32/dumpVram ([rbflush], gs_vulkan_bridge.cpp.bak_rbflush) - patch still present. Not the (only) cause.
- Remaining suspects: (1) save happens before the preceding uploads/draws are visible in paraLLEl-GS VRAM (async batching beyond flush; try wait_idle / synchronous transfer flush); (2) readLocalToHostCT32 coordinate/format mapping for regions holding T8/T4/CT16S data (needs bijection with the CT32 re-upload incl. x0/y0, fbw units); (3) the StoreImage descriptor (img.width/height/x/y/vram_width) parsed wrong.
- Next step: in readLocalToHostCT32 dump the first rows read vs a direct texdec decode, and log sbp/fbw/x/y/w/h for each StoreImage call (the existing [storeimg-vk] stderr line does this for the first 80 calls - check the 'logged' run output).
- Debug aids added this session (remove before shipping): [vramw] in gs_frontend.cpp (.bak_vramw), [rbflush] in gs_vulkan_bridge.cpp (flush is harmless; may stay).

## ISS-125 / CL-0094 — Menus zoomed 2x vertically + ghosted (FIXED 2026-11-01)
- Symptom: menu screens (SMODE2=3 INT=1 FFMD=1, DISPLAY DH=255, 640x256 field buffers) showed the top half of the menu stretched ~2.4x with a deinterlace-blend ghost. VRAM buffers (FBP 0 / 0x50, fbw 10) decoded clean; PCSX2 showed the correct full menu -> presenter bug, not game logic.
- Cause: runtime writes DISPLAY.DH in buffer/field lines (255 = 256 lines) but paraLLEl-GS compute_circuit_rect treats DH as display (frame) lines and halves it for interlaced FFMD (128 lines read, stretched to 512). Same class of bug as the earlier DW/MAGH convention fix.
- Fix: gs_vulkan_bridge.cpp fixDisplay(): when SMODE2.INT && FFMD and DH+1 <= 288, DH = (DH+1)*2-1 ([dhfix]; backup gs_vulkan_bridge.cpp.bak_dh). Verified in-game: Locker Room menu fully framed, no ghost.
- Follow-up: the old 224-line 640x448 crop path in presentFrame is now bypassed for field-mode screens; re-check gameplay (224-line fields) framing.

## ISS-126 / CL-0095 — Menu cursor skips a line per press; extra players in Exhibition (FIX APPLIED 2026-11-01, awaiting in-game verify)
- Symptom (user): pressing Down in match rules moves two lines; Exhibition shows 1st and 5th player (as if two controllers press at once).
- Cause: PSPadBackend::readState ignored port/slot, so every pad port/slot returned the same keyboard/gamepad state; the game polled several pads and applied the press to each.
- Fix: ps2_pad.cpp readState: only port 0 / slot 0 gets real input; every other port/slot returns an idle pad ([padport], backup ps2_pad.cpp.bak_port).
- Verify: Down moves one line in match rules; Exhibition no longer shows the extra 5th player. If the game still treats pad 2 as connected, report disconnected via PadGetState for port != 0.

## ISS-127 / CL-0096 — Entrances: permanent black screen in matches (FIX APPLIED 2026-11-01, awaiting verify)
- Symptom: after starting an Exhibition match the window stays black for minutes while the game keeps running (VU1/matchg active, threads alive). Log: `[movie] request ptid=11 idx=16` -> `playing NMVP001.AFS entry 16` -> immediately `diag abort=1 pkts=0 ... finished (0 frames)`; the movie object (87E220) stays at state 3 forever.
- Cause 1 (hook): movieSkipHook (0x19BCC0) is called once per movie object per frame, but its state (s_started/s_playObj) is global. In matches several objects (st=1/4/6) call it; the `else` branch stopped the native player whenever ANY object was not in state 3 -> entrance movie killed after 0 packets and never ended (no PLAYEND), so the game waited forever.
- Cause 2 (presenter): svr06_movie_active() stays true after the thread finishes until acked; the hook never acked, so svr06_movie_present kept clearing the window to black and drawing the (empty) movie texture over the match every frame. Titantron movies (NMVP000-004, ptid 10-14, 224x192) are in-world screens and must never be drawn fullscreen.
- Fix: (a) hook: stop only when the object that is playing (s_playObj) leaves state 3; other objects polling the hook return early; (b) SvrMovie.cpp: svr06_movie_set_inworld(): for ptid 10-14 the native player only decodes for the game's clock (done/PLAYEND) and svr06_movie_active() reports false so nothing is painted over the scene ([entfix]; backups svr06_overrides.cpp.bak_ent, SvrMovie.cpp.bak_ent).
- Verify: start Exhibition, entrance appears (ramp/lights/walk) instead of black; entrance ends and the match starts. TODO after: Titantron picture on the in-world screen needs the game's own IPU path (ISS-122 macroblock zeros).

### ISS-127 update (2026-11-01 01:45) — verified
- After the [entfix] build the entrance Titantron movies (NMVP000 entries 2/3, 3.7-5.2 MB, 1100 frames) run to `[movie] ended -> PLAYEND`, the match/demo scene keeps rendering and advancing (frame 41k+, 11+ min, no stall, no black overlay). Entrance scenes draw the wrestler on the ramp with beams; PCSX2 reference (snaps 233901) is also dark/blue, so darkness itself is expected.
- Still wrong: Titantron picture (game's IPU path gives zero macroblocks, ISS-122), blotchy/blurred arena textures, green beam colour not compared against reference yet.
- Percentages: Entrances 16 -> 30.

## ISS-126 verification
User confirmed 2026-11-01: match-rules menu no longer skips lines (port>0 pad idle).

## ISS-128 entrance data audit (2026-11-01)
See docs/ENTRANCE_AUDIT.md. Roster record decoded, 129 EVT packages, 34/100 Titantron slots are placeholders, STG 23 small. Package id per wrestler needs runtime capture.

## ISS-128 follow-up
Audit also notes alt asset pair (HHH 5,6) and default music id 124 (Kane, Carlito, Tajiri, Dupree, Spike).

## ISS-129 / CL-0097 VU EATAN missing argument reduction (2026-11-01)
Symptom: wrestler bone rotations wrong (exploded body parts in match, missing head/legs in entrances). Cause: ps2_vu1_lower.cpp vuEatan() evaluated pi/4 + poly(t) directly on t = y/x; the minimax polynomial is for the reduced argument r=(a-1)/(a+1) (atan(a)=pi/4+atan(r)), so EATAN(0)=pi/4 instead of 0. Euler extraction (atan2 VU0 microprogram at 0x078 via vcallms, used by 0x10CDA0, called from key loader 0x20F940) returned wrong angles: live type-7 channels showed cur=(x,y,-x), A=(x,~0,~0). Fix: reduce argument, odd symmetry for negatives. Backup ps2_vu1_lower.cpp.bak_eatan. Awaiting rebuild verification.
ISS-129 result (2026-11-01 02:10): rebuilt with the EATAN reduction; live channels unchanged (type-7: cur=(x,~0.1,-x), A=(x,~0.1,~0)), so EATAN was a real bug but not THE cause of the bad poses. Next: verify the VU0 atan2 microprogram (VU0 addr 0x078, ELF 0x30C60C, 16 insns, called by 0x10CDA0) and the twist redistribution in key loader 0x20F940 (x'=x, z'=(x+z)/2-x) with an exact reference; r5900emu lacks VU0 micro mode.
ISS-129 verification (2026-11-01 09:05): [atanwatch] probe on 0x10CDA0 (overrides, backup .bak_atan): 109/109 calls match libm atan2 within 0.01 rad after the EATAN fix. Type-7 channels now carry real 3-axis values (e.g. cur=(-49.3,3.4,70.7)); remaining relation cur.z = A.z - A.x comes from the twist code in 0x20F940 (plain EE code). Visual result awaiting user check.

## ISS-130 Wrestler/ring/crowd textures scrambled in our VRAM, intact in PCSX2 (2026-11-01) [HIGH] STATUS: open, root-cause area narrowed
Evidence (tools/gifreplay_tex.py = replays host->local transfers in stream order and decodes each draw's texture; analysis_tmp/rep_pc.png vs rep_ours.png / w1.png):
- PCSX2 HHH entrance dump: its 4 MB VRAM state is the last 4 MB before the GIF stream (offset 0x52e203-8192-4194304 in the decompressed .gs.zst, verified: face, chest, arms, trunks decode correctly with tools/texdec.py).
- Ours (live_vram.bin during the entrance): the same draw keys (T8 tfx3, tbp 15328..16208, CLUTs cbp 15638/16272/16276/16280...) decode as structured-but-wrong colours. CLUT blocks around cbp 16276 hold 16-bit-looking words (low half zero) and some CLUT blocks are all zero. Shapes/index data are mostly right (ring apron "SMACKDOWN" logo recognisable), palettes are wrong => CLUT region is what is clobbered.
- The whole range 13344..16300 is the area the game saves with sceGsExecStoreImage (vramw_save.txt: sbp 0x3420, 0x3bc0, 0x3bd0, 0x3c00, 0x3dc0, 0x1e00; 256x32..256x256, sbw 4) and restores from RAM every frame (same mechanism as ISS-122/ISS-124). So ISS-124 (HUD patch), ISS-122 (Titantron/IPU) and the scrambled wrestler skin palettes very likely share ONE cause: the save/restore round trip (readLocalToHostCT32 -> RAM -> CT32 re-upload) is not bit-exact.
- Both gifcap captures show only 4..270 transfers per frame; textures are resident, so end-of-frame VRAM is the right thing to decode. Lighting is not the problem (vertex colours match PCSX2).
Next: (1) log a hash of every StoreImage readback (sbp,sbw,w,h) and of every CT32 upload to the same region and diff them; (2) compare readLocalToHostCT32 against dumpVram for the same rectangle in the same call; (3) test a wait_idle before map_vram_read and a CT32 round trip self test at startup; (4) if the readback is exact, check that the restore upload and a following draw/transfer (16x16 tile uploads to dbp 15296 fbw 4 seen in the capture) are not overwriting the saved pages.
Tools: tools/gifreplay_tex.py, tools/pcsx2gs.py, analysis_tmp/texsheet_pc.py, rep_pc.png, w1.png.

### ISS-130 update (2026-11-01 10:20)
- Every frame the game restores a 256x256 CT32 image to dbp 15296 fbw 4 as 256 tiles of 16x16 (1 KB each); that covers textures AND their CLUTs (blocks 15296..16319, CLUT 16272..16300). The restore changes all 1024 blocks vs. the VRAM dump taken just before, so the saved RAM buffer is the source of truth for the region.
- Decoding the saved RAM buffers (StoreImage dst addresses from [storeimg-vk], e.g. 9CA9E0, A0BB10, CAFA60, D2FC80) as VRAM: buffer #47 (A0BB10) has a coherent blue palette, #51/#55-#57 have the same index image with a garbled CLUT block. So the garbage is captured at save time (or already present in the restored data), not created by the restore.
- Applied: GS.cpp sceGsExecStoreImage now runs processPendingTransfers() + gifArbiter().drain() before readLocalToHostCT32 (backup GS.cpp.bak_sidrain). Result: apron/crowd/skin textures still garbled in the entrance capture, so stale queued transfers were not the (only) cause.
- No uploadImage() calls with dbp 7000..16400 ever occur (rthash.txt has only SV lines), so texture/CLUT uploads reach paraLLEl-GS through gifTransfer; check PSMT8/PSMT4 IMAGE handling and CLUT (CSM1) uploads there.
- Debug probes still in: [rthash] (gs_vulkan_bridge.cpp.bak_rthash), [evtrd], [atanwatch], [vramw].
- Next: capture the stream during a scene load (gifcap must be armed before load, N large) and decode each PSMT8 transfer + its CLUT upload directly from the stream, compare to the PCSX2 dump of the same texture to find where the data first diverges.

### ISS-130 update 2 (2026-11-01 10:30)
- Stream capture of 40000 GIF packets (analysis_tmp/g_load.bin, no uploadImage markers): ALL host->local transfers are CT32 (PSMT8/PSMT4 textures are uploaded as CT32 raw swizzled data). PCSX2 does the same restores (15296 fbw4 256x256, 16x192 stripes, 8x8 CLUT blocks), so the game logic matches; only chunking differs (ours 256 tiles of 16x16).
- NEW DIFFERENCE: the PCSX2 HHH entrance frame renders ~1000+ tiny TSTRIPs (3x4 px) into FRAME fbp 15296 fbw 1 **psm 10 (CT16S)**; our capture has only 8 SPRITEs into 15296 (psm 0). Something that the game renders into a 16-bit scratch target at 15296 every frame (and then saves/restores around) is absent or different in ours. Check whether the VU1 program that generates these strips runs, and whether our captures were taken at a different point of the frame/entrance (same point needed).
- Also: single-block 8x8 saves of CLUT blocks (sbp 3bd1, 3ce4..3ce6, 3be2.. etc.) come back all zero (nzQ=0), i.e. those CLUT blocks are empty in VRAM at save time.

### ISS-130 update 3 (2026-11-01 10:40)
- PCSX2 HHH entrance frame: 10012 of 41354 draws render into FRAME fbp 15296 fbw 1 CT16S, each a TSTRIP/SPRITE sampling T8 64x64 textures at tbp 13200..13328 (CLUT cbp 14560), i.e. the game composes small texture tiles into a 64-px-wide 16-bit scratch target at 15296 every frame. In every one of our captures (entrance, loading, match start) NO draw targets fbp 15296 - the same textures (e.g. 12384/13632/13664) are only drawn as ordinary arena geometry into the main frame buffer (fbp 0).
- Therefore the scratch/composite pass is missing in our run (or never triggers), while the save/restore around it still happens. Next: find the EE routine that sets FRAME_x to 15296/CT16S (grep game code for 0x...3BC0 as FRAME, or watch GS FRAME writes with fbp 0x1DE), see why it is skipped (flag, VU1 program not generating the strips, or a DMA chain not run) and compare with PCSX2 via the savestate.

### ISS-130 update 4 (2026-11-01 10:45)
- Correction: our 40000-packet load capture (g_load.bin) DOES contain 24990 draws into FRAME fbp 15296 fbw 1 CT16S, so the composite pass exists in our run - but only while loading. In steady entrance/arena frames ours has none, while PCSX2's steady entrance frame has ~10000 per frame.
- The EE builds FRAME packets 0x0A0101DE (fbp 15296, fbw 1, CT16S) in RAM at 0x1D3CB70.. (37 copies, 0x570-byte stride) = the display lists exist; find which routine submits them and why it is skipped in steady state (per-frame gate flag/counter).

### ISS-130 update 5 (2026-11-01 10:50) KEY FINDING
- Retraction: the CT16S scratch pass (draws into FRAME fbp 15296 fbw1 psm10) DOES run in ours (4998 draws per 12000 packets in the entrance); the earlier "missing pass" came from short capture windows. The [chainlog] shows the 0x1D3C000..0x1D41000 display lists are walked continuously.
- PCSX2 Undertaker entrance dump (20261029233901): VRAM state decoded with the same decoder shows tbp 15616 T8 256x256 + CLUT 16276 = the wrestler's SKIN texture (plus 15872/16280 and 16064/16288 skin pieces). Same draw keys as ours (key 15616/4/256x256/cbp 16276 and 15872, 16064, 16136, 16000).
- Ours (same entrance, same keys): VRAM at 15616 holds the ring apron/SMACKDOWN logo index data and the CLUT at 16276 is noise. I.e. the wrestler skin textures are NOT in VRAM; a stale image of another scene (apron) sits in their slots. This is exactly what the per-frame 256x256 restore from the saved RAM buffer would produce if the buffer was saved before the skin upload (or the skin upload never happened after the save).
- Next: find when the skin texture is uploaded (CT32 transfer to dbp 15616.. fbw 4 / the 16x16 tile loop) relative to the StoreImage of sbp 3BC0, and why in ours the save precedes the upload (game waits on a DMA/sema/event we complete in a different order). Compare with PCSX2 savestate event order. Tools: analysis_tmp/pc_u_vram.bin (PCSX2 Undertaker VRAM), analysis_tmp/u_cmp.png.

## ISS-131 Unlock All (2026-11-01) [MEDIUM] STATUS: groundwork only
- The profile save (mc0/BESLES-53676SD7, 901,288 bytes) is loaded verbatim into guest RAM at 0x3D9BF8 (file offset N -> RAM 0x3D9BF8+N). The 160-byte roster records at 0x3D9C26 are the first part of that buffer (file offset 0x2E), i.e. roster data lives in the save, not a static table. PCSX2's RAM (user savestate 17:25) and ours hold the same buffer except ~600 bytes of runtime state, so lock/unlock state is inside this buffer or the game's own tables, not different between the two runs.
- No lock flag found in the 78 roster records (fields 0x81/0x82/0x89 split 57/21, 49/29, 41/37 but correlate with staff/women/legends, not an obvious lock). Blank-named slots #12,13,15,16,19,20,25,38,68,70,75,76 may be hidden/unlockable characters.
- CodeBreaker "Unlock All" codes exist for the US version only and are encrypted (almarsguides); not usable for PAL.
- Plan: (1) in the live build buy one Shop item / unlock one character and diff the 901 KB buffer before/after (svrlive ramdump), repeat per category to map the bit arrays; (2) add a runtime setting (Options "Unlock All") that ORs those bits after save load, and emit the same bytes as a PCSX2 .pnach for SLES-53676 so the PCSX2 reference playthrough can select every wrestler.
- Locker Room is reachable in the live build (Main menu -> Locker Room works); Shop/Unlockables screen not yet reached.

## Tooling: tools/pcsx2cap.py (2026-11-01)
`pcsx2cap.py ingest` turns every PCSX2 .gs.zst dump into vram.bin, gif.bin, sheet.png, frame.png and summary.json under analysis/pcsx2_cap/<label>_<stamp>/ (tested on the Oct-29 Undertaker/HHH dumps: 36 dumps in ~2 min); `pcsx2cap.py grab` copies eeMemory/vu1/scratchpad from the newest PCSX2 savestate. Intended protocol: Claude presses 'Save Single Frame GS Dump' + 'Save State' hotkeys through computer-use every ~3 s while the user plays each entrance, then runs grab/ingest, so the user plays each entrance once.

### ISS-131 update (2026-11-01): shop purchase = save-buffer bits
- Shop item table at RAM 0x592020, stride 0x28: [id, id2, id3, 0, ?, 0, category, buyable, price(+0x20), owned(+0x24)]. Static; SOLD state is NOT stored in it. Price poke (svrlive poke 0x592090 0) lets you buy with 0 cash.
- Purchasing 3 legends changed only these save-buffer bytes (buffer base RAM 0x3D9BF8): +0x6A384 = 1 (counter), +0x6A38C and mirror +0x6A39E: 0x08 -> 0x18 -> 0x38 (bit 3+n per legend bought), +0x6A3B9 and mirror +0x6A3CB = 0x20. Dumps: analysis/dumps/shop0..3.bin.
- TODO: confirm in Exhibition roster that these bits unlock wrestlers; find extent (other categories), then implement Unlock All setting + .pnach.

### ISS-131 addendum (2026-11-01): Unlock All only covered shop items 0..95 / category<=9
Shop table has 137 entries (0..136), categories up to 17. Loop limit and category guard (`e[6] > 9`) stopped at item 67+, leaving categories 10-17 (attire, moves, etc.) locked. Fixed in SvrConsole.cpp: loop to 137, guard `e[6] > 20`, bitmap loop `i < 137`. Backup SvrConsole.cpp.bak_cheats2. Unverified in-game.

## ISS-132 Locker Room graphical glitches (2026-11-01) [MEDIUM] STATUS: open, evidence gathered
Symptoms (user + Vulkan dumps analysis/shots/vk_00014..17.ppm): Shop background nearly black; empty Trophy Case view shows a large dark streaky skewed panel; some textures blocky/garbled; colour flicker while navigating.
Capture: gifcap 12000 + vramdump in Locker Room (analysis_tmp/lr_gif.bin, lr_vram.bin, lr_sheet.png via tools/gifreplay_tex.py; tools/gifreplay_tex2.py also prints TEX0 fields). 27 transfers only (all to dbp 7680 fbw4 CT32, 256x32/128 = frame-buffer-sized restores), no texture uploads in window => textures are resident from room load.
Findings: belts/trophy/wood/gold textures decode correctly. The three most-used textures decode as salt-and-pepper noise: key (tbp 12768,tbw4,T4 256x256,cbp 12897), and two T8 keys cbp 12572/12592/12392 region neighbours (see lr_sheet.png tiles 1, 7, 10). All use CLD=1, CSM1, CPSM=CT32 => most likely wrong CLUT/index data in VRAM (same family as ISS-130 save/restore corruption) rather than a draw-state problem. Noise could also be genuine, so a PCSX2 reference of the same screen is needed to decide.
Next: PCSX2 GS dump (Save Single Frame GS Dump) + screenshot of Locker Room Shop / Trophy Case; decode with pcsx2cap.py ingest and compare texture keys 12768/12897 etc.

### ISS-132 update (2026-11-01 12:50)
- PCSX2 reference captured (analysis/pcsx2_cap/locker_20261101123110: frame.png, gif.bin, vram.bin): Wall of Fame view = lit gray walls/ceiling/floor. Hotkeys already bound in PCSX2: Shift+F8 GS dump, F8 screenshot. The speckled "noise" textures decode the same in PCSX2 (decoder limitation, not corruption).
- Applied: direct HLE register writes (applyGsClearPacket in GS.cpp, applyGsRegPairs in Support.h) now processPendingTransfers()+gifArbiter().drain() first so they stay ordered after queued GIF data (backups *.bak_wrdrain). Diagnostic `[gsvk] direct regs` shows ~8 regs per clear (PRIM, RGBAQ, XYZ2, XYOFFSET, TEST_1/2, FRAME...) written ~4000x/min.
- Result: more of the room now renders (couch, table, wall, lamp) in analysis/shots/vk_00020.ppm, but walls show black/white triangle pattern (one triangle of each quad dark) + very dark lighting => per-vertex colour (VU1 lighting output) wrong on alternate triangles. Next: capture VU1 program/vertex colours for a wall strip (`[vudump]`) and compare with PCSX2 gif.bin vertex RGBAQ values.
- New tool: tools/gifvtx.py (vertex XY/ADC stats from a gif capture).
- Vertex colour statistics (tools/gifrgba.py, textured-draw RGBAQ at kick): ours (analysis_tmp/lr2_gif.bin) 16.3% of vertices near-black (<8 per channel), PCSX2 (same room) 3.3%; bucket r<32: ours 12928 vs PCSX2 6180. Supports "VU1 lighting colour output too dark / ambient missing" for static room meshes. Next: dump the VU1 lighting program inputs (ambient + light colours/directions in VU1 data memory) for a wall draw in both emulators (ours: [vudump]; PCSX2: vu1Memory from savestate via pcsx2cap.py grab).
- ISS-132 update (13:05): After the ordering fix the room is lit and visible (vk_00025.ppm Entertainment Center view: bright, walls present) but the wall covering shows a large black/white TRIANGLE pattern = wall TEXTURE content/sampling wrong, not vertex colour (earlier "dark" read was a different camera view; vertex-colour stats are not comparable across views). Candidate: key (tbp 12768,tbw4,T4 256x256,cbp 12897, ~11.5k vertices in PCSX2 frame too). VRAM bytes at 12768..12895 differ from PCSX2's vram.bin snapshot (0.7% equal) while both are non-zero, and neither capture re-uploads it => texture was loaded differently (different data or different swizzle) in ours. Next: compare against PCSX2 at the same view; decode properly (tools/texdec.py assumed layout gives noise in BOTH, so first fix/validate the decoder on a known T4 texture); test with software GS (RUN_SOFTWARE.cmd) to see if the pattern is paraLLEl-GS specific.
- LESSON: computer-use actions hide svr06run.exe (willHide) -> raylib presenter stalls ("[gsvk-hang] no present"). Restore with scripts\restore_game_window.ps1. Avoid computer-use while testing the game.
- PCSX2 keyboard cannot drive the game (pad is bound to the user's controller); use savestates/user input for PCSX2 captures.
- ISS-132 update (13:30): ruled out: (a) vertex colours/lighting (view-dependent stats), (b) the 12768 T4 texture = film-grain overlay (speckled in PCSX2 too, ~11.5k verts), (c) STQ Q=0 draws (966 draws in ours, all-zero vertices at XY (0,0) = unwritten VU1 buffer slots kicked as degenerate triangles; invisible; not in PCSX2 because...unknown but harmless), (d) CLIP/FCAND/DIV code review matches PCSX2 semantics. Tools: tools/gifst.py, gifst2.py, gifst3.py (STQ/Q stats).
- Decoded texture sheet for our room frame (analysis_tmp/lr2_sheet.png): most textures fine; several wood/wall textures show vertical dropout streaks / blotches / cyan or pink speckle = partially wrong CLUT/index pages (tiles r2c4, r3c3, r4c1, r4c3, r8c1, r9c1) -> same family as ISS-130 (texture VRAM content not bit-exact). Strongest remaining lead: ISS-130 save/restore of VRAM regions (the game saves and re-uploads regions each frame; room textures that live in/near restored regions get stale data).

### ISS-132 update (2026-11-01 evening): VU1 findings, PINE, PCSX2 reference capture
- PINE (PCSX2 slot 0, port 28011) works: tools/pine.py (status/title/save/load/r32/dump). A full 32 MB EE RAM dump takes ~2 s (batched Read64, 40000 ops/packet). PCSX2 must stay open; only PINE + user hotkeys (F1 state, Shift+F8 GS dump) are used.
- Fast VU1 path (runFast) now keeps hardware latency for MAC/status/clip flags and Q (m_fastPipe, default on; flag bit 11 = 2048 disables) and can use exact long-double FMAC math (m_fastExact, flag bit 12 = 4096). With both on, runChecked (flag 10) shows ZERO fast-vs-exact mismatches on completed programs (only budget-limited 65536-cycle idle programs differ). Side effect: the loading bar works now. The Locker Room black boxes are NOT a fast-vs-exact VU1 arithmetic problem.
- PCSX2 reference (analysis/pcsx2_cap/locker_desk_20261101201259: gif.bin, vram.bin, eeMemory.bin, vu1Memory.bin, vu1MicroMem.bin; tools/giftl.py, p2sx.py): PCSX2 desk frame has 8320 STQ draws, 0 with Q==0, 0 vertices outside +-2048; ours has 536 of 4999 with Q==0 (all-zero vertices at XY 0,0) and 1147 vertices far outside. So some VU1 programs emit zero/garbage vertices in ours -> degenerate triangles = missing wall/object pieces.
- VU1 micro memory: our copy has all-zero 8-byte pairs (227 in the first 8 KB) where PCSX2 holds the pair ff020008 3c030080 (a NOP pair with the T bit). The pattern is not present in the EE RAM source in either emulator, so it is written by something else (hardware/PCSX2-side padding or an EE write path we drop). Unresolved: check whether the game writes VU1 code memory directly with SQ or via a VIF MPG chunk split across DMA tags (ps2_vif1_interpreter.cpp VIF_MPG skips the whole MPG when pos+copyBytes > sizeBytes).
- Never-ending VU1 programs ([spin] diag): ~35/s, each resumes at the previous end PC; loop counter vi2.. increases without bound while vi3=-32768 (garbage loop limit) -> VU1 data memory header wrong for those runs. Rare, so not the speed problem.
- SPEED: whole game ~4 fps although the window says 60. One thread at ~96% CPU, ~90% of it in VU1 emulation ([tprof] vu1run, double-counted before the fix). ~8.8k VU1 runs/s. Optimisations applied (CL-0054): upper-NOP early return, decoded pair by reference, no per-run double timing; new [vufast] 3 s line (runs, spins, avg cycles, ns/cycle). User target: match PCSX2 speed.
- New bug reported: video previews do not play in the player (ISS-133).
- Left-over test switches: flag 9 (exact VU1) makes the game extremely slow; always set flag 9/10/12 back to 0 after tests.

## ISS-133 Video preview in player does not play (2026-11-01) [MEDIUM] STATUS: open
- User report: previews for videos in the player (menu video previews) do not play in the recomp. Not yet investigated (likely IPU/MPEG HLE path, see ISS-122).

### ISS-132 / perf update (2026-11-01 late) — VU1 spin loops are ~80% of VU1 time
- `[vufast]` stats in the Locker Room: ~200 VU1 programs/s never reach their E-bit and burn the full 65536-cycle budget, continued again via MSCNT. That is ~80% of all VU1 cycles and the main reason the game shows "60 fps" but plays at ~4 fps.
- The stuck loops are `IBNE viN, vi0` counter loops (main one: pc 0x148–0x358, `IBNE vi5,vi0` at 0x350) whose count, read from VU data qword ~0x320, is 0 or garbage (e.g. 13179 counting down by ~1000 per budget). In a PCSX2 PINE dump of the same scene this count is populated. Same root as the missing walls: some VU1 batches get missing/wrong input data (cf. old A-03 garbage bone/camera matrices).
- Also seen: our VU1 micro memory lacks PCSX2's `ff020008 3c030080` pad pairs (origin unknown).
- **Band-aid (ps2_vu1_core.cpp runFast, `[runaway]` log):** every 4096 cycles look for a repeating PC pattern containing `IBNE viN,vi0`; re-check 4096 cycles later; if the counter moves away from 0, or the remaining count would need >150000 cycles, zero the counter right before its IBNE so the loop exits (works for any step size). Backups: `.bak_runaway`, `.bak_runaway2`.
- **Real fix still to do:** find why the batch data is missing upstream (VIF UNPACK of the count/matrix qwords, MPG split across DMA tags, EE-side packet build) by comparing vu1Memory with PCSX2 via PINE.

### ISS-130 / ISS-122 update (2026-11-02 ~00:40) — entrance + tip screen, VU1 cleared, font slot 0x3dc0
- New tool tools/vu1check (vu1check.exe): replays a PCSX2 savestate's VU1 state (VF/VI/ACC/Q/I/flags at Internal Structures offset 0x3d20, TPC=VI[26]*8, vu1Memory, vu1MicroMem) through OUR VU1 interpreter. Locker Room state (TPC 0x28b8, TOP 722) and Undertaker entrance state (TPC 0x2638): output byte-identical to PCSX2 (0 differing qwords). => geometry/VU1 math is not the entrance/texture problem.
- PCSX2 savestate GS.bin: VRAM starts at offset 425 (4 MB). Entrance VRAM ours vs PCSX2: ~75 identical 8 KB pages; PCSX2 has 165 zero pages, ours 12.
- Tip screen ("Stamina", after the entrances): text drawn as T4 tbp 0x3dc0 tbw4 256x512 CLUT 0x3f44. PCSX2: font atlas there + white/alpha CLUT. Ours: other texture data + dark image palette => text renders as dark blocks.
- Frame order in ours is correct: ~10k draws into 0x3dc0 as CT16S scratch (fbw1), then 3 restores (CT32 256x128 to 0x3dc0 from RAM 0x744EB0), then text draws. The restored copy is wrong because the save (vramw_save seq 51098119, sbp 3dc0 256x128) captured VRAM right after the game uploaded a PSMT8 256x128 image to 0x3dc0 (+CLUT 0x3f40) and a PSMT8 256x256 image to 0x3e40 (+CLUT 0x3f44) (vramw.txt seq 51098116..119). All earlier [savedump] copies of 0x3dc0 (this and previous runs) also lack the font.
- PCSX2 entrance RAM keeps the font rect at heap 0x11D9A70 (91% match to its VRAM rect); ours restores from static 0x744EB0.
- Conclusion so far: the font atlas is never present in 0x3dc0 when the game saves it in ours; same family as ISS-122 (per-frame atlas upload with ZERO payload, producer missing; [skipzero] drops it). Missing reference: a PCSX2 Shift+F8 GS dump of the tip screen (shows what PCSX2 uploads to 0x3dc0 and from which data).
- Also seen: first LoadImage after boot is dbp 0x3bb0 dbw8 T8 512x512 from RAM 0x630230 with an all-zero source (2x) - probably an intentional VRAM clear/reservation, not yet confirmed.

### ISS-130 / ISS-122 update (2026-11-02 ~00:50) — PCSX2 tip-screen GS dumps + allocator log
- User captured ~500 PCSX2 GS dumps (00:08-00:13 entrances, 00:13:14-15 tip screen, 00:30-00:32). Ingested 8 into analysis/pcsx2_cap/ent2_* (tools/pcsx2cap.py ingest --snaps analysis/pcsx2_snaps_sel).
- PCSX2 tip frame has the SAME per-frame structure as ours: ~6.6k draws into 0x3dc0 as CT16S scratch, 3 restores of 0x3dc0 (CT32 256x128), then 291 text draws (T4 0x3dc0 / CLUT 0x3f44). Only the restored DATA differs (PCSX2 = font, ours = an 8-bit image).
- PCSX2 entrance RAM: no texture records with tbp >= 0x3d40; ours: many (T8 256x128 at 0x3dc0/0x3e40/0x3e80/0x3ec0, CLUTs 0x3f40..0x3fbf).
- Allocator log (live `diag vmgr on`; managers are stack objects, [878]=start [87A]=end [880]=cursor): entrance load creates managers 0x3DC0-0x3FBF (a0=1FFF300: T8 256x128 at 0x3DC0, 256x16 at 0x3E40 + CLUT), 0x3BC0-0x3DBF, 0x3C00-0x3DFF, 0x3C00-0x3E00, 0x3C44-0x3E44, 0x3CE4-0x3EE4. Menu load used 0x3420-0x3C1B, 0x1E00-0x25FB, 0x2DFC-0x2E7B. The resource-bank flush (21B250) uploads each bank then saves it (1B67E0 -> sceGsExecStoreImage), so the 0x3dc0 save holding the T8 image is by design for that bank.
- Open question: why the tip screen restores 0x3dc0 from RAM 0x744EB0 (the entrance bank's copy) instead of a buffer holding the font (PCSX2: heap 0x11D9A70). Candidates: the tip/font bank is never loaded/uploaded+saved in ours (file load or async state), or the restore pointer table is stale. Next: log 1B67E0/StoreImage (sbp, dst, ra) and the restore uploads' source EE address for every bank change from match select to the tip screen, uncapped for sbp 3dc0.

### ISS-130 / ISS-122 update (2026-11-02 ~01:15) — wrong bank DATA, not wrong VRAM handling
- Build 00:59 adds: uncapped [storeimg-vk] for sbp>=0x3BC0 (with content hash), [bigref] (sources of big VIF1/GIF payloads), [vu1dump] (VU1 state at first zero-count loops -> build_svr06b_clang/analysis/live/vu1dump_N.bin: 8-word header, VU1State, vi@entry[16], data 16K, code 16K).
- Tip screen: restore source = RAM 0x744EB0 (DMA REF data=0x744E40 qwc 8199). It is filled by save #136 (sbp 3DC0 256x128, hash F6BF2860) - identical content to the earlier save #104 at scene load. Each such save is preceded by the bank upload: PSMT8 256x128 -> 0x3DC0 (+CLUT 0x3F40), PSMT8 -> 0x3E40 (+CLUT 0x3F44). The tip text samples exactly 0x3DC0 T4 / CLUT 0x3F44, so this bank IS the font bank. In PCSX2 its data is the font with a white/alpha CLUT; in ours the uploaded data is a picture with a dark palette => the game loads the WRONG DATA into the font bank (file/resource/offset divergence), the GS/VRAM side is behaving correctly.
- Our RAM holds a font CLUT ramp at 0x73A610 (static area); PCSX2 holds it in heap 0x11E9EB0/0x12C91E0. RAM layouts differ too much to compare by address.
- VU1 (Locker Room, vu1dump_0): loop count is read by `ILW.x vi5, 800(vi0)` / `ILW.y vi11, 800(vi0)` at 0xA0/0xA8 (header qword 0x320); runs that spin enter the 0x148..0x358 loop from other segments (MSCNT from 0x28B8 -> B 0x2340 -> ...) with stale vi5; dumps 2/3 show qwords 0x31E.. overwritten with (0,0,0,FLT_MAX) output.
- Next: log LoadImage (GS.cpp) source address + the file/resource it came from for dbp >= 0x3D00 (lift the 400-line cap), and the VFS/CD reads that fill that source; find the same font file in the disc image and check which resource index/name the game asked for vs got.

### ISS-130 / ISS-122 update (2026-11-02 ~01:45) - font data in RAM is CORRECT; corruption is between upload and save/restore
- New build logs every disc read ([cdlog]: lbn, sectors, host file, offset, RAM dst, PAST_EOF flag) and LoadImage for dbp>=0x3D00. Only PAST_EOF hits are 5 sector-rounding reads (benign).
- The bank at VRAM 0x3DC0 gets TWO uploads: (1) PSMT8 256x128 from RAM 0x16B6C90 (palette 0x3F40 from 0x16BEC90), later (2) PSMT4 256x512 from RAM 0x729C50 with palette 0x3F44 from 0x739C50. Upload (2) data decoded from a RAM dump (analysis/dumps/tip3_ram.bin, rendered analysis/entrance_shots/font4_tip3.png) is the correct font with the white/alpha ramp CLUT, i.e. the game loads the right data.
- Source buffer of upload (1) was later overwritten by a SOUND.PAC read (0x16A2C80, 0x24800 bytes) - normal buffer reuse.
- StoreImage saves of sbp 3DC0 256x128 (PSMT8 geometry) occur at #72 (dst 0x1696020) and #74 (dst 0x7444F0); the bank is later restored from those. Hypothesis: save happens when VRAM still holds the stale picture (upload 1) or before upload 2 is applied by paraLLEl-GS (queued vs readback ordering), or format mismatch PSMT4 font read back as PSMT8. NEXT: shared sequence counter in LoadImage/StoreImage/restore logs to prove order; compare VRAM at 3DC0 right after upload (2).
- Locker Room: vu1dump_1..3 VU data memory is mostly (0,0,0,FLT_MAX) = div-by-zero clamp results of all-zero geometry input.

### ISS-130 / ISS-122 update (2026-11-02 ~02:15) - sequence-number build; tip screen text now correct, draw lists identical to PCSX2
- Build with shared order numbers (g_svr06Ord) on LoadImage/[storeimg-vk]/[bigref], plus [cdlog] disc-read log. Order in this run: font upload PSMT4 256x512 -> 3DC0 (#913) + CLUT 3F44 (#914), bank save #81 (#917, correct), restore REF qwc 8199 (#933). Live VRAM decoded (tools/texdec.py, entrance_shots/vram_font_tip4.png): font intact. User screenshot: tip text now renders correctly (earlier runs broken => run/path dependent; watch for recurrence).
- Remaining tip-screen differences vs PCSX2 (frame_ent2_001314): backdrop shows raw/garbled 3D scene instead of dark blurred arena, translucent dark panel and title/Continue frame end-caps missing.
- Draw comparison (analysis/dumps/tip4_gifcap.bin vs pcsx2_cap/ent2_20261102001314/gif.bin via tools/gifdump3.py): identical draw kinds and states; counts are exactly 4:1 (PCSX2 dump = 4 frames, ours 1: font sprites 1392 vs 348, flat 64 vs 16, CT32 fb-copy sprites 32 vs 8). So nothing is dropped at GIF level; remaining differences are in rendering of identical draws (blend/test/fb-as-texture) or in the 3D scene content.
- NEXT: replay the PCSX2 dump through our paraLLEl-GS path (parallel-gs-replayer reference) to separate renderer bugs from game-side data; inspect framebuffer-as-texture sprites (FRAME 1080100, tbp 0 CT32 512x512).

### ISS-130 / ISS-122 update (2026-11-02 ~02:15) - tip screen is correct on the SOFTWARE GS: bug is in the Vulkan (paraLLEl-GS) path
- Same build run with SVR06_GS=software (RUN_SOFTWARE.cmd): tip screen ("Advanced Camera Controls") shows dark tinted backdrop, dark translucent panel, title/Continue bar end-caps - matches PCSX2 (screenshot analysis/entrance_shots/sw_tip_screen.png, taken mid-fade into Now Loading).
- Vulkan path on the identical draw stream (draw lists proven identical to PCSX2, 4:1 frame ratio) shows bright/garbled raw 3D scene, ghost text, no panel, no end-caps.
- => game data and GIF stream are fine; the defect is in how paraLLEl-GS (via gs_vulkan_bridge) renders: suspects are framebuffer-as-texture sprites (FRAME 1080100, TEX0 tbp 0 CT32 512x512), the translucent flat sprite pass, feedback/hazard handling, and fb copy ordering. SVR06_VK_NOFEEDBACK/NOMIP switches exist (see ISS-122 hypotheses).
- NEXT: draw-bisect on the tip screen (render only first N draws in Vulkan vs software), identify first diverging draw, then fix in bridge/ubershader state setup. Compare against the DiscoStarslayer/parallel-gs fork for related fixes.

### ISS-130 / ISS-122 update (2026-11-02 ~03:40) - draw/packet bisect tool built and used; backdrop is a stale scene snapshot, not a single bad draw
- New diag (build 02:23): console `svrlive raw "drawlimit N"` (N packets per presentation interval, -1 off) drops every GIF packet after the N-th in BOTH the Vulkan bridge (gif_transfer intake) and the software frontend (processGIFPacket); logs `[drawlimit] interval packets=..` to run_err.txt. Helper: scripts/_sweep.ps1 -Tag X -Limits a,b,c (screenshots to analysis/entrance_shots/sweep_*.png). Remove before shipping (gs_frontend.cpp/.bak_dlim, gs_vulkan_bridge.cpp/.bak_dlim, SvrConsole.cpp/.bak_dlim).
- Tip-screen interval is ~3250-6130 packets (varies per present, ~5760 typical). In this run the Vulkan tip screen (Advanced Camera Controls) rendered close to PCSX2 at every limit >= ~300 (dark panel + crowd backdrop); only residual: light-blue ghost outlines of UI bars over the text. The earlier bright/garbled backdrop (first no-limit shot, and the Stamina screenshots) depends on WHAT the game snapshotted as backdrop (stale/garbled 3D scene from the previous screen), i.e. the backdrop problem is inherited from the scene render (entrance stage / Locker Room geometry+texture issues), plus the darkening/blur pass.
- Conclusion: stop chasing the tip panel as a separate bug. Fix order: (1) 3D scene correctness in Vulkan (Locker Room zero-input VU1 batches ISS-132, entrance textures ISS-130), (2) UI bar 9-slice offsets (title/Continue bar caps misplaced even in software GS), (3) ghost bar outlines.

### ISS-132 ROOT CAUSE FOUND + FIXED (2026-11-02 07:28 build) - JR/JALR used the stale "branch VI delay" value
- Evidence: [btring] VU1 branch-trace ring (vu1branch_0.txt) for the first runaway: stage 28b8 (clipper set-up at 0x2340) took the clip path, ran `IADDIU vi1, vi0, 840` then `JALR vi13, vi1` at 0x2808, and jumped to 0x0F0 (= old vi1 30, a vertex pointer from `ILW.w vi1,-6(vi7)`) instead of 840*8 = 0x1A40 (recursive clip routine). It landed inside the 0x60 program's 160-vertex loop with vi5 = 0 -> counted away from 0 (the "runaway"), wrote FLT_MAX garbage over VU data (the (0,0,0,FLT_MAX) pattern), corrupted the clip stack at qword 824 and the q800 control block, so later runs failed too. Same pattern at 0x2768/0x2770 (`IADDIU vi12,580 / JALR vi13,vi12`).
- Cause: ps2_vu1_lower.cpp JR/JALR read the target through readBranchVi(), i.e. the "IBxx sees the pre-write VI value" model. PCSX2 applies that VI-delay backup only to the conditional branches (analyzeBranchVI from mVUanalyzeBranch1/2); JR/JALR stall on the register and use the current value.
- Fix: JR/JALR read m_state.vi[is] directly (IBxx unchanged). Backup ps2_vu1_lower.cpp.bak_jrvi.
- Result: Locker Room renders complete (walls, ceiling rails, lockers, trophy case, couches, desk) matching PCSX2; [runaway] 0 (was 40-60 per visit), [xgfail] 0 (was 2524 per run); loading-tip backdrop now shows the full scene; steel-cage attract demo renders at 60 FPS. Only the triangle-clipping path was affected, which is why geometry near the camera (walls) turned into black wedges.
- Ruled out on the way: zero-source UNPACKs ([zrace] probe found none), microcode differences vs PCSX2 (only padding slots differ: zeros vs NOP/LOI filler).
- STATUS: fixed (verify other scenes: entrances ISS-130 may share the cause). Diagnostics still in the build: [zrace] (vif1), [btring]/vu1pre/vu1branch (vu1 core), [runring], [unp320] - remove before shipping.

## ISS-134 NOW LOADING hang going main menu -> Locker Room after an attract demo (2026-11-02 07:40) [MEDIUM] STATUS: open
- Seen once: attract steel-cage demo -> title -> main menu -> Locker Room; NOW LOADING for 3+ min, last reads LOCKER.PAC, no further cdlog/gabig/resload, main loop alive (watchdog loops rising), mainWaitFlag=0. The direct route (title -> menu -> Locker Room without a demo) loaded fine twice. Likely the same family as findings/hang-demo-load (timing dependent). Evidence: analysis/hang_locker_20261102_0740_run_err.txt, RAM dump dumps/hang_locker_0738.bin.

## ISS-135 Triple H entrance: main thread dies on a null virtual call (freed motion node) (2026-11-02) [HIGH] STATUS: open, diagnosed
- User report: game froze during the Triple H entrance (after the JALR fix, build of 10:48).
- Log (`run_err.txt` ~line 47384): `[guest-branch:missing-target] JALR source=0x105004 target=0x0`, then `[sched] REAL thread 1 finished (pc=0) entry=100008`. The main thread returned, every other thread then waits forever (watchdog STUCK, th1 st=5); the presenter keeps showing the last (scrambled) frame at 60 FPS.
- Cause: guest function 0x104FC0 walks a linked list of motion nodes ('MoDt'/'MoHo' tags) and calls `node->[0x18]->[0x14](node)`. Node 0x13F2A00 is filled with 0xEBEBEBEB (freed/cleared memory) but is still linked from its neighbours (0x13F2990, 0x13F2A70), so the call target is 0. `[addrinfo]` says the block is not in a live heap block and has no free record, so a whole pool was probably cleared while nodes were still linked.
- Call chain at the failure: 0x10C900 -> 0x10D070 -> 0x10D110 -> 0x10BEA0 -> 0x10CDA0 -> 0x10BD20 -> 0x11E230 -> 0x1AFB00 (teardown walk). RAM dump: `analysis/dumps/ram_missing_0_0.bin`.
- Also seen before the failure: `vfprintf error: Invalid file handle` x4 (harmless guest printf to an unmapped FILE*).
- Next: find who clears the pool (watch writes of 0xEB pattern at 0x13F2A00 with `svrlive watch`), and check whether the teardown is triggered early (entrance skip / timing difference vs PCSX2). A temporary fallback (skip indirect calls to address 0 instead of finishing the thread) would keep the game alive but hide the bug.

### ISS-130 update 5 (2026-11-02 11:25, after CL-0101 build)
- CORRECTION: the recomp window was the REAL Triple H roster entrance; Create an Entrance was only run in PCSX2 (reference). Recomp real Triple H entrance vs PCSX2 Create-an-Entrance Triple H: recomp runs at 57-59 FPS, no freeze this run (ISS-135 not reproduced), but the arena/ramp/crowd textures are still scrambled (palette-noise look) and the wrestler is not drawn properly. PCSX2 shows a correct Triple H. The JALR fix did NOT cure ISS-130.
- Create an Entrance has NOT been tested in the recomp yet. Captured at the entrance: `analysis/live/live_vram.bin` (VRAM) and `gifcap.bin` (12000 GIF packets, 17:25). `flag 20 1` made no visible change.
- Previous run (real Triple H entrance) crashed with ISS-135 instead.

### ISS-130 update 6 (2026-11-02 11:50) decode of the recomp Triple H entrance vs PCSX2 (ent2_20261102001135)
- The recomp draws the SAME skin texture slots PCSX2 uses (T8 tbp 15616 tbw4 cbp 16292/16304, tbp 15464 cbp 16292, T4 16064/16128/16176...), so draw setup matches. The VRAM contents at those slots are wrong in the recomp (edge-map/noise instead of skin).
- `vramw.txt` (upload log, 6251 uploads): the skin slot 15616 is uploaded correctly as T8 256x256 at seq 1626561, then at seq 1627988 a 16x16 CT32 (CLUT-sized, 1 KB) transfer is written to DBP 15616 (BITBLTBUF `00013d0000010000`, TRXREG 0x10x0x10) - i.e. a CLUT upload whose base address is inside the live skin texture. Same pattern may overwrite other slots; check all CT32 16x16 uploads whose DBP is also a live TBP.
- Hypothesis: the game's texture-slot manager believes 15616 is free (residency/refcount state differs from PCSX2) and places a CLUT there. Next: find the guest function that picks the CLUT address for that upload (trace the GIF packet back to its EE caller via the DMA tag/ra) and compare its inputs with PCSX2's save state eeMemory.
- Not conclusive: the PCSX2 capture is a different moment of the entrance, VRAM layout (TBPs) differs in places.

### ISS-130 update 7 (2026-11-02 12:05) the skin set is a 256x256 CT32 "save/restore" image, and the restored data is wrong
- Per frame the game re-uploads a 256x256 CT32 image to dbp 15296 (covers VRAM blocks 15296..16319, i.e. the whole character skin/CLUT region). PCSX2 does the same (4-10 per dumped set); the recomp draws the same skin TBP/CBP slots.
- Payload comparison (gifcap.bin vs PCSX2 gif.bin of ent2_20261102001135): the recomp's 256x256 payload (identical in both uploads, 100% equal bytes) is a banded/stripe pattern; PCSX2's payload contains the wrestler's face and skin patches. So the bytes the game hands to the GS are already wrong before upload.
- The game fills that EE buffer with `sceGsExecStoreImage` (GS local->host readback, logged as SAVE in `vramw_save.txt`: sbp 0x3bd0 256x256 first seen seq 1626162, followed by 8x8 CLUT saves 0x3bfd, 0x3d06, 0x3d07). The recomp serves these via `gsvk::readLocalToHostCT32` (gs_vulkan_bridge.cpp). Either the VRAM it reads (paraLLEl-GS native swizzled copy) does not yet hold the skin at save time, or the PSMCT32 addressing/ordering of the readback differs from the real GS (the image is read back as CT32 although the area holds T8/T4 + CLUT data).
- Next: log the first 64 bytes of each 0x3bd0 256x256 SAVE and compare with the same area of `live_vram.bin` taken just before; verify `addrPSMCT32` against ps2tek page/block/column order for a 256-wide read with sbw=4; then check whether skin textures (T8 uploads at 15616 seq 1626561) are present in VRAM at the SAVE seq. The old `save_*.bin` files in analysis/live are from earlier runs (many seq values) - delete them before the next capture.

### ISS-130 update 8 (2026-11-02) - MTGS-wait before sceGsExecStoreImage readback: NOT the cause
- Change: GS.cpp now calls ::svr06MtgsWaitIdle() after gifArbiter().drain() in sceGsExecStoreImage (backup GS.cpp.bak_mtgsrb). Build 0 errors.
- Test: Exhibition Single, Undertaker vs Triple H, Play -> arena/entrance still fully scrambled (blue/white corrupted textures, green block artefacts) in the recomp. Fix did not change the symptom.
- Next candidates: readback addressing for non-page-aligned sbp (0x3bd0) in gsvk::readLocalToHostCT32; save (seq 1626162) happening before the skin upload (seq 1626561); FRAME_2 fbp=15296 render-target overlapping the saved region; GIF-replay harness to A/B against PCSX2 captures.

## ISS-137 No commentary, ring announcer or crowd audio (2026-11-02) [HIGH] STATUS: partly fixed (playback works, trigger open)
- Symptom: the match has no play-by-play commentary, no ring announcer and no crowd noise. Only music (native ADX) is heard.
- Finding 1 - the assets: every speech/crowd archive under `BGM/` is CRI ADX, the same format as the music the native
  player already streams, so no new decoder was needed. `PBPPAC.AFS` = 4728 commentary lines (8-digit ids, leading digit
  1-4 = which commentator), `ANNOUNCE.AFS` = 686 Lilian Garcia (`LG*`) + 686 Tony Chimel (`TC*`), `CHEER.AFS` = 152 crowd
  beds, `BG_ENV.AFS` = 8 arena ambience beds, `SSND1/2.AFS` = SFX banks (almost all entries are 67-byte
  `dummy24mono.ahx` placeholders). Full table in `findings/audio-findings.md`.
- Finding 2 - why nothing plays: two independent dead ends. (a) The IOP sound driver never reaches voice playback (no
  `[spu2] KON` of a real sample, ISS-136). (b) The EE-side CRI path cannot address the archives at all, because the ADXF
  partition table at `0x3129D0` is all zero under HLE - the same root cause that broke movies (ISS-010). So the game never
  even asks for a clip through `ADXT_StartAfs`; the request is pushed to the IOP as sound state instead (SIF RPC fn 0x69,
  sid 0x301, a 2368-byte block sent every call).
- Remedy (shipped): native voice playback, following the music/movie HLE pattern.
  - `Kernel/Stubs/SvrMusic.cpp`: slots 6 -> 16 so music + crowd + ambience + commentary + announcer can overlap;
    self-freeing one-shot slots (`reapLocked`); last-resort reuse of a finished slot instead of dropping a clip;
    AFS **name table** reader (`"AFS\0"`, u32 count, count x (u32 off,u32 size), then (u32 metaOff,u32 metaSize); the
    metadata block holds one 48-byte record per entry whose first 32 bytes are the clip's file name) so clips are
    addressable by their real names. New API: `svr06_afs_entry_by_name`, `svr06_voice_play`, `svr06_voice_play_name`,
    `svr06_voice_stop`, `svr06_voice_playing`, `svr06_voice_reap`, `svr06_voice_free_slots`.
  - `Kernel/Stubs/SvrConsole.cpp`: `voice <pbp|ann|cheer|env|sfx|cas|BGM/X.AFS> <clip|entry> [gain] [loop]` and
    `voicestop <handle>`, so any clip can be played in the running game without a rebuild.
  - `overrides/svr06_overrides.cpp`: hook on `0x147A98` (the only writer of the partition table, i.e. the ADXF partition
    registrar) captures ptid -> archive at runtime instead of guessing ids; `adxStartAfsHook` now plays any registered
    non-BGM/non-movie partition (ptid >= 18) natively with the game's own handle (so Stop/GetStat/SetOutVol keep
    working) and logs every unmapped ptid; `SVR06_VOICE_PTID="22:PBPPAC,18:ANNOUNCE"` forces the mapping by hand.
  - `Kernel/Syscalls/RPC.cpp`: `[snd69d]` logs only the words that CHANGE in the fn 0x69 sound-state block, which is how
    the sound-request fields (channel state, clip id) are to be identified.
- Verified (build 19:20, run at frame ~2400): `voice pbp 10030010` -> `playing BGM/PBPPAC.AFS entry 0`, `voice ann LG001`
  -> entry 0, `voice ann TC001` -> entry 686 (exactly the 686 LG + 686 TC split), log shows the name tables loaded
  (4696/1369 unique names) and `[music] decoding ... (adpcm_adx 24000 Hz 1 ch)`. So decode + mixing + addressing work.
- Open: the automatic trigger. The partition hook did NOT fire (no `[voice] partition ptid=` line), confirming the game's
  ADXF partition load never runs, so `ADXT_StartAfs` will never be called for voice. Next: run a match with the
  `[snd69d]` diff log, find the fields that carry a clip/sound id, and drive `svr06_voice_play` from them; `PAC/PBPLIST.PAC`
  (DPAC group `PBP2`, one 117760-byte `DATA` blob, binary - not text) holds the play-by-play id lists and still needs
  its record layout worked out. The announcer's wrestler-to-`LG***` mapping is also still unknown.

### ISS-137 update 1 (2026-11-02 19:55) ring-announcer table found, sequence playback added
- `PAC/SOUND.PAC` (DPAC, **data base `0x4000`**) group ADX entry **`ANNO`** (2304 B at `0x209000`) is the ring-announcer
  table: 10-byte records `[id, nameClip, 109, 112, flag]`, id 0,1,2,... for the first ~89 records. The clip numbers index
  `BGM/ANNOUNCE.AFS`, all inside the LG (Lilian Garcia) half 0..685, so Tony Chimel = same clip **+686**. Entry `CHER`
  (1280 B at `0x208800`) is the matching crowd table; `LIST` + `B1*`/`B2*` (19.8 MB) are the SPU sample banks;
  `REFM`/`REFF` are referee voices.
- Added: `svr06_voice_play_seq()` plays several clips of one archive back to back on one slot (a `seqBusy` flag keeps the
  reaper off the slot between clips), and `svr06_announce(nameClip, chimel, gain)` plays `[nameClip, 109, 112]` (+686 for
  Chimel). Console: `announce <nameClip> [tc] [gain]`.
- Verified (build 19:54): `announce 131 0` -> `ANNOUNCE.AFS` entries 131, 109 and 112 decode and play in order.
- Still open: the trigger. `[snd69d]` confirms the EE asks for sounds over SIF RPC fn 0x69 (fields decoded in
  `findings/audio-findings.md`), but the ids seen so far are menu sounds; a real match is needed to catch the announcer
  and commentary ids. `PAC/PBPLIST.PAC` is BPE-compressed and still unread.

### Pose / bent legs update (2026-11-03 00:20, build 23:57 with CL-0107/0108)
- User: textures correct now; legs look stuck in a sitting/crouched pose while walking (pre-existing, previously hidden by flailing). Screenshots analysis/shots/shot_22/23.png (HHH lunging/crouched, Taker hunched).
- [ckptloop]: only 0x1AC780 (4x4 matrix-array multiply, loop top = entry; caller 0x1AC960) hit the checkpoint-as-return case (8 times per match) -> CL-0108 is correct but rare; not the cause of the crouch.
- tools/legtrack.py over 6 live RAM dumps 0.7 s apart (analysis/posetrk_0..5.bin): both wrestler skeletons (0x18C3B90 115 bones, 0x196B4A0 103 bones) DO animate (knees -24..-82 deg, thighs changing), root world y changes. Root local height t.y = -9.41 vs leg chain ~10 units => knees always bent.
- Hypothesis (unverified): foot IK / root height (hips too low vs floor target) or blend weights; needs a PCSX2 PINE comparison of the same skeleton fields during a walk (root t, knee/thigh euler) - tools/pine.py + legtrack.py.

## ISS-138 Player 2 controller not recognised (2026-11-03) [HIGH] STATUS: fixed in build 00:41, awaiting user verification
- Cause: ISS-126 limited pad reads to port 1. Remedy: CL-0109 (per-player device/keyboard/bindings, PCSX2-style). Code: ps2_pad.cpp svr06input::{bindingFor,gamepadFor,keyboardFor}; menu: ps2_ship_menu.cpp Controls page.

### ISS-130 / CL-0107 / CL-0108 summary (2026-11-03)
- ISS-130 FIXED (user-confirmed everything renders correctly in Vulkan): GS HLE sceGsExecLoadImage (Kernel/Stubs/GS.cpp) + bytesForPixels (Helpers/Support.h) used wrong bpp for T4HL/T8H/CT24 and ignored the 15-bit NLOOP limit.
- Limb flailing: dispatchGuestBranch (ps2_runtime.cpp) checkpoint-as-stub-return bug; hit at 0x1AC780 (caller 0x1AC960). CL-0108.
- Open: ISS-139 wrestler legs stuck in crouch (skeletons 0x18C3B90 115 bones, 0x196B4A0 103 bones; root local t.y=-9.41 vs ~10-unit leg chain; PCSX2 idle hipH 8.4-9.0, knees 21-33 deg); need recomp time series via tools/pcpine.py + legmetric_dump.py.
- Recomp PINE (Kernel/Stubs/SvrPine.cpp) default port 28011 clashes with PCSX2 tooling and returns stale RAM - to fix.

### ISS-139 update (2026-11-03 00:55) - crouch localized to thigh Y rotation
- Recomp (10 dumps, tools/rcpose_dump.py on analysis/posetrk_*.bin, local matrices at skel+0xA0, Euler R=Rx*Ry*Rz) vs PCSX2 idle (analysis/pc_pose_ts3.txt, pc_metric.txt):
  - rootT.y: recomp -9.24..-9.97, PCSX2 -9.26..-9.97 -> MATCH (root height is not the cause).
  - Knee (b3/b8 y): recomp -21..-37, PCSX2 -9..-33 -> close.
  - Thigh L (b1) y: recomp +44..+47, PCSX2 +7..+9. Thigh R (b6) y: recomp +55..+70, PCSX2 +17..+19. Same on both wrestlers -> consistent offset of about +37..+50 deg on the thigh Y channel (hipH recomp 7.1-7.6 vs PCSX2 8.4-9.0).
- Conclusion: thigh Y euler is wrong in the recomp while root/knee match. Next: find the writer of skel+0xA0 matrix 1 and 6 (euler build) and the source channel (key loader / blend / IK) for those joints. Tools: tools/rcpose_dump.py, tools/pcpine.py pose.

### ISS-139 update 2 (2026-11-03 01:10) - RETRACTION of "thigh Y +40 deg offset"; channels checked
- The +40 deg thigh-Y conclusion above compared recomp frames against PCSX2 idle frames taken at different animation phases and is NOT established. A fresh PCSX2 dump (analysis/pc_reg_now.bin, skeletons 0x18F3BA0 / 0x199B4B0) shows thigh Y 17..38 deg and knees -50..-61 deg while moving, i.e. the same range as the recomp.
- Motion owners (MoHo magic 0x4D6F486F): recomp 0x18CFA70 / 0x1976470, PCSX2 now 0x18FFA80 / 0x19A6480 (earlier PCSX2 state 0x18CFA70 / 0x1976470); both cnt 21, mode 2/0, type tables identical. Channel arrays: owner+0x20, 0x180 bytes each, cur +0x00, A +0x30, B +0x90, type +0x174. Tools: tools/mohoch.py (FILE BASE NCH), tools/skscan.py (FILE BASE), tools/bonedef_dump.py, tools/rcpose_dump.py.
- Recomp channels DO animate (type-7 ch6/ch10 x sweeps 20 -> -147 -> -167 -> 20 over 10 dumps; ch7/ch11 thigh y 39..45 nearly constant). The local-matrix euler (b1 y 44..68, knees -21..-37) varies far less than PCSX2 (knees 0..75 in 1 s for the walking wrestler).
- Remaining hypothesis: leg joints in the recomp barely swing (walk amplitude lost) rather than a constant offset. Needs a same-phase comparison: PCSX2 and recomp sampled at the same animation (e.g. both wrestlers idle on entrance/ring-select pose, or fixed frame via savestate) and a per-bone dump of channel->matrix over one walk cycle.

### ISS-139 update 3 (2026-11-03 05:25) - same-phase idle capture (analysis/idle_rc.bin recomp vs analysis/idle_pc.bin PCSX2, both wrestlers idle)
- Bone local-matrix euler (deg), wrestler 1 (115 bones): recomp b1=(-5,46,-12) b3=-35 b6=(15,68,24) b8=-23; PCSX2 b1=(-4,7,-9) b3=-26 b6=(6,17,12) b8=-30. Wrestler 2 (103 bones): recomp b1=(-13,44,-17) b3=-37 b6=(21,54,25) b8=-30; PCSX2 b1=(-10,0,-8) b3=-8 b6=(9,3,8) b8=-9. rootT.y matches (-9.3/-9.9 both). => at the same idle phase the recomp thighs are rotated ~40-50 deg more on Y: the original +40 deg thigh-Y finding stands for IDLE (update 2 retraction applied only to the non-matching walking comparison).
- Owner channels (recomp owner 0x18CFA70 vs PCSX2 0x18FFA80): ch4 identical (-23,-7,2); ch7/ch11 (type 3 thigh pitch) close (8,48 / 3,48 vs 5,53 / 5,53); type-7 channels ch6/ch10/ch13 differ by ~175-189 deg in x (recomp -146/149/3, PCSX2 43/-36/178). These are the first same-phase channel differences found; type-7 = 16-bit x with cur.z=-x. Suspect: type-7 channel value (or its wrap/sign) feeding the thigh rotation. Needs: which bone nodes consume ch6/ch10/ch13 (writer of node+0x10 matrices via 0x20D9B0 / 0x1BEC50 path) and raw key s16 for these channels from the motion record (PAC/M.PAC) to see whether the recomp key loader 0x20F940 decodes them differently.

## ISS-140 Triple H head/arms/legs show as damaged at match start, no hits taken (2026-11-03 05:22) [HIGH] STATUS: open, new report
- User report: with no interaction and no damage from the other player, Triple H's head, arms and legs are already damaged when the match starts.
- Link to ISS-139 (unproven): if the per-limb damage state starts non-zero, the engine would pick hurt/limp stances and the legs would stay in a crouched pose, which fits "legs fixed in a sitting pose, wrestlers only, referee fine".
- Plan: locate the per-wrestler limb-damage fields (Chara size 0x810; hit/reaction state at Chara+0x1DC per COLLISION_MAP.md; hit gate 0x5F9C40) in the idle dumps (analysis/idle_rc.bin vs analysis/idle_pc.bin), compare against PCSX2 at match start, then trace the writer.
- Tooling note: tools/autowatch.py now arms on skeletons with 103/115 bones only (.bak_n keeps the old filter); the earlier watch hit a 91-bone skeleton (not a wrestler); its hits came from 0x1B69A8 / 0x1B67E0 via 0x114560.

### ISS-140 update (2026-11-03 05:45) - HUD limb damage red at start (head+arms+legs, later head only); PCSX2 hit capture
- User: Triple H's damage indicators for head/arms/legs were red at match start with no hits, later only head red (values change over time without interaction).
- PCSX2 reference capture: 20 dumps at ~4 s (analysis/pcd_0..19.bin = EE 0x1700000+0x900000, pce_0..19.bin = 0x400000+0x200000) while the user hit Triple H's head. Persistent step changes after the hit: 0x185BAD0 record: words +0x0C/+0x10..: 0,0,0,0,0,0,0,3,0,3 -> 0,0,3,2,2,2,2,1,2,1 (0x185BADC 0->2, 0x185BAEC 3->1, 0x185BAF4 3->1; same shape at 0x185BBD0, 0xF4 further = second wrestler or second slot); 0x17E33B4 0->13 with 0x17E33B8..0x17E33CC 0->1 (five 1-flags); 0x1D2DC28..0x1D2DD58 and 0x1E6DC38.. mirrored blocks 0->14/61 (3822 -> 14). Not yet identified which is the limb-damage array; recomp addresses are shifted (skeleton 0x18C3B90 vs PCSX2 0x18F3BA0, +0x30010), pattern search for the 0x185BAD0 record in recomp dmg_3.bin found no match.
- Recomp 4-dump diff (analysis/dmg_0..3.bin, decaying floats): decaying large values at 0x55BB0C (41.3->33.4), 0x55BB34/38 (127.8->118.8) in the overlay data area - unverified leads.
- Next: run the same head-hit procedure on the recomp (hit once, dumps via svrlive ramdump) and diff, since matching by pattern failed; or trace writers (hw watch) of the fields above.

## ISS-141 SQRT.S translated with the wrong source register (2026-11-03) [HIGH] STATUS: fixed (CL-0110), build pending verification
- Symptom class: any game math through the sqrtf helper 0x10CC80 (returns f0, argument f12) got sqrt of a stale register: distance/length/normalise results wrong in match code (callers game overlay 0x4E47D8, MainGame 0x5D553C/0x60D8C8, main 0x270E48), plus 0x1457A4/0x145860 (sub_00145788) and 0x248830 (sub_00248770).
- Cause: EE encodes SQRT.S as fd, ft; fpu_translator.cpp used fs. Found by the hardware-model differential test (CL-0110).
- Remedy: translator emits ft; generated files sub_0010CC80, sub_00145788, sub_00248770 patched. Possible relation to ISS-139 (crouched legs) and collision distance offsets - check in the next play session.

## ISS-142 EE FPU not bit-exact with hardware (2026-11-03) [MEDIUM] STATUS: fixed (CL-0110)
- 81 of 901 hardware test cases differed (exponent-255 add/sub, multiplier 1-ulp quirk, madd/msub overflow rule, div/sqrt/rsqrt rounding, max/min). Remedy: ps2_ee_fpu.h, 0/901 now.

### ISS-143 Extra music slots (user feature, 2026-11-03)  [FEATURE]
- Request: Enhancements > "Allows extra music slots" checkbox; when on, extra songs from a `Custom Music` folder appear in Create-an-Entrance music selection and are playable; when off, exact 1:1. Must support the future launcher importer (songs from other SvR games, names/metadata for the now-playing popup).
- Findings (RAM dump analysis/dumps/entrance2.bin, Entrance Set-up screen): song names are fixed-size records in the loaded menu text database around RAM 0x9C1F10.. ("Bang Bang / Bumpy Knuckles", ...), followed by 8 placeholder records "BGM NAME 057".."BGM NAME 064" (0x9C2190..0x9C2200, 16-byte records) and album records ("Their Rock is Not Our Rock"... then "ALBUM NAME 057".."064" at 0x9C2570..0x9C25E0). So the stock game has 65 music slots (000-064) of which 057-064 are unused. No direct pointers to these strings exist (text is reached through the database's own index).
- Music playback is fully native already: `adxStartAfsHook` (0x156D18) receives (ptid 0-9 = BGM banks, fid) and plays through SvrMusic.cpp, so a custom slot can be served by mapping (ptid,fid) of a virtual slot to a host file.
- Next: find the code that builds the entrance music list and its slot count (watch reads of the "BGM NAME" records while the list is open), and the slot -> (ptid,fid) table.
- Update 2026-11-03 13:15 (live trace while the user previewed songs in Create-an-Entrance > Music, game kept running):
  - Preview path: sound manager vtable entry at ELF 0x27D258 / RAM 0x37D058 -> `0x1D2390(mgr, index, a2, chan)` (sets mgr+0xCC=3, +0xD0=-1, calls 0x1D1E50, 0x1CCE10, then `0x1CAC00`) -> `0x1CA4A0(mgr, category, linearIndex)` writes the current request globals `[gp-0x69A0]`=0x39A0D0 (bank/ptid) and `[gp-0x699C]`=0x39A0D4 (track/fid): **ptid = mgr[+0x4C + 4*category] + idx / mgr[+0x70 + 4*category], fid = idx % mgr[+0x70 + 4*category]** -> `0x1C2790` -> `0x1C1720` (sound object, music object at RAM 0x1F85160 in this run; +0x44 = ADXT handle 0x3150A0, +0x74 state, +0x188 ptid byte) -> `0x156CC8` -> ADXT_StartAfs `0x156D18` (native hook).
  - So a theme id maps to (BGM2 + id/40, id%40): ids 0-39 = BGM2.AFS, 40-66 = BGM3.AFS. Ids 67-79 would land on ptid 3 fid 27-39 (beyond BGM3's 27 entries, no conflict) and can be served by the native hook as custom songs; ids >= 80 would reach ptid 4 (= BGM1ENVB), so higher custom ids need a 0x1CA4A0 hook that returns virtual ptids.
  - Still open: the Create-an-Entrance list builder (86 entries alphabetical: 64 theme names + "BGM 01-17" + 4 + "none"; list order is not bank order, the list index -> music id table was not found as a plain byte/u16/u32 array in RAM or the ELF; user DB note says the roster -> bgm order is stored in PAC/MISC.PAC).
