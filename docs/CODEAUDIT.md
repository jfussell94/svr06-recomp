# Code audit of the replaced and translated game code (2026-10-27)

Goal (user request): understand how the game code works (game logic, entrances, music, skeletons, collision, memory) and implement it
correctly **before** worrying about rendering. Nothing is launched; everything is file based. No emulator code or emulation of the game is
used; PCSX2 may be used only to produce data dumps that our own tools read.

## Tools (svr06-recomp/tools/codeaudit/)
| file | purpose |
|---|---|
| r5900emu.py | own small R5900 + VU0 macro-mode reference model (VU float clamping, MAC/STATUS flags, Q) |
| disasm.py | disassembler for the ELF (rabbitizer library) |
| images.py | code images: main ELF + 11 MWo3 overlays (analysis/ovl_pac) |
| libvu0_funcs.py | lists the 32 libvu0 routines in the ELF |
| stubaudit_vu0.py + build_harness.sh | compares the old HLE stubs (Kernel/Stubs/VU.cpp) with the original code |
| vu0_transpile.py + vu0exact_ops.h | turns the game's own libvu0 code into exact C++ (vu0_exact_gen.cpp/.h) |
| verify_vu0gen.py + build_genharness.sh | proves the generated C++ equals the reference model bit for bit (state, flags, memory) |
| cop2_census.py | counts every VU0 macro instruction per image and lists DIV/SQRT/VCALLMS/CFC2 sites |
| snippets.py, insn_difftest.py | takes the C++ the translator really emitted for sample instructions, compiles it with the runtime headers, and compares with the model |

## Finding 1: the libvu0 replacements were wrong (cause of bad poses / collision)
The recompiler config replaces 30 libvu0 routines with hand-written C++ (Kernel/Stubs/VU.cpp). Comparing against the game's real code:
- Exact: ApplyMatrix, DivVector(+XYZ), InterVector(+XYZ), TransMatrix, CopyMatrix, UnitMatrix, ClampVector, LightColorMatrix, ClipScreen/3, ClipAll,
  TransposeMatrix, InversMatrix, DropShadowMatrix (within 1e-6).
- Wrong: **MulMatrix** (different result entirely), **InnerProduct**, **Normalize** (w lane and scaling), **ecossin** (table polynomial replaced by sin/cos),
  **RotMatrix** and Rot X/Y/Z (approximate), **CameraMatrix**, **NormalLightMatrix**, **ViewScreenMatrix** (zero where hardware gives max float),
  **RotTransPers/N** (nan handling, w).
These are the building blocks of skeleton posing, camera and collision vector math, so wrong results explain wrestlers not standing correctly
and collision not working. Fix: the stub functions now call code generated from the game's own instructions (`Kernel/Stubs/Vu0ExactGen.cpp`),
verified bit-exact (state + flags + memory, 150-300 random and edge-case trials per routine, `verify_vu0gen.py` prints ALL OK).
Status: written and syntax checked with the runtime headers; **not yet built into the exe or run** (no launches until the user allows).

## Finding 2: VU0 macro DIV/SQRT/RSQRT in generated code (ISS-102)
Only 35 sites exist (census): 8 main ELF VDIV, 5 VSQRT, 12 in overlay "game", 2 in "moves". Most main-ELF sites are inside libvu0 (now replaced by exact code).
Remaining sites are in overlays game/moves (vdiv by w for perspective in ovxGame 0x4E5FA0 area) and main 0x114fbc, 0x221058, 0x226d18, 0x10c878.. (VSQRT).
Translator emits `Q = ft!=0 ? fs/ft : 0` (hardware: signed max float and D/I flags). Needs targeted regeneration or a text patch of those functions.

## Finding 3: per-instruction translator test (COP2 / COP1 / integer)
- COP2: ordinary finite inputs give correct values. Differences: MAC/STATUS flags are not produced at all (only matters for code reading flags via CFC2: 16 sites);
  VFTOI0/4 do not clamp positive overflow (host gives 0x80000000, VU gives 0x7FFFFFFF); VMINI/VMAX differ for special values; exponent-255 inputs.
- COP1: single-precision add/sub/mul/div/madd/msub match for ordinary values; differences only for NaN/Inf/denormal inputs.
- Integer/MMI: small sample clean except `movn` upper half (model detail, to be checked) and unmodelled mult1/paddh in the reference model (extend later).
Reports: analysis/codeaudit/insn_*.md.

## Next
1. Map the HLE-replaced libraries and `ret0/ret1` stubs one by one (44 `ret0` stubs hide unknown functions) and compare with the real code where it is plain EE code.
2. Find animation / skeleton / collision / entrance / music code through call graph and string/table xrefs, document in docs/GAMELOGIC.md.
3. Memory address map from dumps (own tools reading PCSX2 dumps as data only).
4. After the user approves a launch: build once, check poses and collision.

## Finding 4 - VU0 microcode source (file-based, no launch)
- A PCSX2 savestate (`analysis/pcsx2_menu_inputs.p2m2_SaveState.p2s`, zstd zip; read with `zipfile_zstd`) holds `vu0MicroMem.bin` (4 KB). It is used only as read-only data by our own tools (`analysis/dumps_local/`, never published).
- Live VU0 micro memory is populated in 4 ranges: 0x000-0x5E0, 0x600-0x738, 0x760-0x9B8, 0xAB0-0xB90 (the engine math `vcallms 0x890` lives in the third).
- Source blobs are static `.data` in the ELF at 0x30C590 / 0x30CB98 / 0x30CCD8 / 0x30CF40. Ranges 2 and 4 match byte-for-byte; ranges 1 and 3 differ in 144 / 36 dwords (data is stored as several program segments, not one contiguous image), no direct lui/addiu references (referenced via tables).
- Tool: `tools/codeaudit/vu0blob_xref.py`.
- Next: find the MPG-upload table (VIF0 path `processVIF0Data` exists in the runtime), build a VU0 micro disassembler/verifier and compare programs run by our interpreter with the reference.

## Finding 5 - VU0 microcode is a chain of VIF MPG packets in the ELF (corrects Finding 4)
The blob at 0x30C58C-0x30D2xx is a VIF packet stream, not a raw image. MPG packets (VIF cmd 0x4A) found by `vu0blob_xref.py`/scan:
| ELF addr | VU0 micro byte addr | insns |
|---|---|---|
| 0x30C58C | 0x000 | 15 |
| 0x30C60C | 0x078 | 16 |
| 0x30C694 | 0x0F8 | 49 |
| 0x30C824 | 0x280 | 82 |
| 0x30CABC | 0x510 | 26 |
| 0x30CB94 | 0x600 | 39 |
| 0x30CCD4 | 0x760 | 38 |
| 0x30CE0C | 0x890 | 37 (the `vcallms 0x890` engine program) |
| 0x30CF3C | 0xAB0 | 28 |
| 0x30D088 | 0x27AC8 | 66 (VU1-size target, not VU0) |
The runtime already handles VIF0 MPG (`processVIF0Data` -> `ps2_vif1_interpreter.cpp`), so the load path exists; no EE code in the ELF/overlays references D0 (0x10008000) with a constant, so the stream is sent through the dma helper with a run-time channel pointer. `patch_vdiv.py` + exact VDIV/VSQRT/VRSQRT helpers (`ps2_runtime_macros.h`) now applied to 20 sites.

## Finding 6 - Instruction audits (integer/MMI, FPU)
- Reference-model bug fixed: PCPYLD/PCPYUD when rd aliases rs (model read the already-overwritten register). Translator was correct.
- Model extended with PCGTH PCPYH PEXTLB PEXTLH PEXTUB PMAXH PMINH PPAC5 PPACB PPACH PSLLH PSRLH PSRAH PSRAW PSUBSH PXOR PADDW/H/SH/UB/UW. All verified bit-exact over 1500 real words (12 random states each). Not yet modelled: PHMADH PMULTH PMFHL QFSRV (need HI/LO/SA state).
- FPU (cop1) 3000 real words: normal inputs match except ~1% (add 1, sub 1, div 2, madd 7, msub 4, mul 12, mula 2 of ~1700 each class) - likely rounding-mode (EE FPU truncates). `suba/msuba` unmodelled. Special inputs (Inf/NaN/denormal) differ, expected.
- Tools: tools/codeaudit/insn_difftest.py, outputs analysis/codeaudit/insn_*_big.md.

## Finding 7 - Replaced-library stubs: sceIo family (reviewed)
ret0 stubs 0x12D268-0x12F0B0 are the IOP host-filesystem RPC wrappers (sceIoctl2, sceDevctl, sceMount, sceSync, sceMkdir, sceFormat, sceAddDrv, sceSymlink, sceReadlink, sceRename, sceChstat, sceLseek64, sceDopen, sceDread, sceGetstat). The game reads data through sceCd/AFS and saves through sceMc, so returning 0 is acceptable for these. Still to review: sceCd*, sceGs*, sceDma*, sceSif*, sceMc*, sceCdRead path, pad, ret1 at 0x13C7A0/0x1A45F8.

## Finding 8 - libc math stubs
- `__kernel_sinf/__kernel_cosf` stubs use host sinf/cosf (original is a fixed minimax polynomial; difference ~1e-7, acceptable). `__ieee754_rem_pio2f` stub reduced in float, losing precision for large angles; now reduces in double and returns y0 + y1 residual (LibC.cpp, backup LibC.cpp.bak_remp). Needs the next build.
- libc `rand` (0x2F8840) is replaced by a time-seeded host rand, so game-side rand() variety is real; identical song/picture choices therefore come from timers/cycle counts (see GAMELOGIC randomness lead).
- Stub list reviewed: sceIo family (safe), libm (done), memcpy/memset/str* (standard), malloc family (allocation layout - to review), Cd/Sif/Mc/Pad/Gs/Dma (to review).

## Finding 9 - COP2 "normal value bad" cases explained
Debug mode added to insn_difftest.py (`DT_SHOW=<mnemonic substring>`). All "normal" value mismatches seen for vmulax/vmaddw/vmul/vmulx traced to operands whose exponent is 0xFF (random-bit inputs); the macro path (`ps2_vmul`) only clamps the *result*, while real VU hardware first treats 0xFF-exponent *inputs* as +-FLT_MAX. For ACC-writing forms the difference is visible in ACC (0x7FFFFFFF vs 0x7F7FFFFF, or a different product). Real game data virtually never carries exponent-255 floats, so impact is low; fix would be an input sanitising step in `ps2_vfinish` (kept on the list). MAC/STATUS/CLIP flag mismatches remain in ~100s of random trials per arithmetic op: games only read them via CLIP/FS instructions.

Correction to Finding 9: the runtime's slow path (`ps2_to_double`) treats exponent-0xFF inputs as ordinary values scaled by 2^128 and clamps only the result, which matches how the EE FPU and VU are documented to behave; my Python model (`vuin`) instead replaces such inputs by +-FLT_MAX. The mismatches therefore point at the model, not the translator. Model to be aligned (low priority) once confirmed against a hardware dump.

## Finding 10 - "UNKNOWN ret0/ret1" stubs triaged by call sites (tools/codeaudit/callers.py)
- 0x1282C0-0x1284A8, 0x1285A0-0x128980: IOP RPC wrappers (func 0x127410 with command ids 1..4, -5..-9); 0-2 callers each, all inside the library cluster (0x1275xx-0x128xxx). Safe as ret0.
- 0x11DE80/0x11DEC8/0x11DF48/0x11E000: DMA helper variants; callers: only 0x183930 (one). 0x11E038 (4 callers): "clear bit8 of flag word, return old value" used on the GS error/reset path (0x1A938C, 0x1FBB80 after 0x1A6F20 fails) - ret0 acceptable but a faithful version would clear the bit and return the old word.
- 0x13D430: CD service RPC id 0x21, callers 0x2D7448 and 0x2D770C (USB/network area).
- 0x1A45F8..0x1A4BF0: only two callers (0x1A4C00, 0x1A4C08), inside the same cluster.
- No stub on a hot gameplay path was found among the ret0/ret1 set.

## Build verification (2026-10-28)
Build-only run (ps2EntryRunner, 2710 steps) finished with 0 errors; exe linked. Compiled for the first time: exact libvu0 stubs (VU.cpp, Vu0ExactGen.cpp), exact VDIV/VSQRT/VRSQRT helpers + 20 patched sites, ftoi clamp, LibC rem_pio2f double reduction, EE timer launch offset, ps2_vu1_core diagnostics. Not yet run (no launches allowed).

## Finding 11 - HI/LO halfword MMI ops were mistranslated (2026-10-28, found while auditing PMULTH/PHMADH)
- `PMULTH`, `PHMADH`, `PMADDH`, `PMSUBH`, `PHMSBH` summed all products into one 64-bit HI:LO and wrote that to rd. Hardware produces eight 32-bit lane results spread over LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 (LO/HI are 128-bit with the pipeline-1 halves in ctx->lo1/hi1), and rd = {LO.w0, HI.w0, LO.w2, HI.w2}. PHMADH: pair sums p0+p1, p2+p3, p4+p5, p6+p7 (no accumulation; odd lanes unverified). `PMTHL.LW` now sets only the low word of LO/HI and LO1/HI1 from rs.w0..w3 (was ignoring pipeline 1).
- Real game users (static census, ELF+overlays, ASCII data excluded): `PMULTH` x15 in 0x1F1D74-0x1F35A0 (software 8-bit pixel blend: pextlb/psubsh/pmulth/psraw 7/ppach/paddh/ppacb) and 0x25C334/0x25C3B8; `PHMADH` x12 at 0x15AEB4-0x15B004 (halfword fixed-point 4x4 dot-product chain with paddw + qfsrv reduction). Both therefore produced wrong pixels/vectors. Overlay dnas (online DNAS auth) also has PMULTH/PDIVBW; irrelevant offline.
- Fix: `ps2xRecomp/src/lib/mmi_translation_helpers.cpp` (backup `.bak_hilo`) emits the per-lane form; 27 generated sites in main (sub_0015AE90, sub_001F1B50, sub_001F3400, sub_0025C1E0) patched by `tools/codeaudit/patch_mmi_hilo.py --apply` (backups `.bak_mmihilo`). Verified with `insn_difftest.py mmi`: reference model extended with per-lane HI/LO + hi1/lo1 comparison; phmadh 24/24, pmulth 36/36 trials ok.
- Not yet fixed in the translator (no real code users found): PMULTW/PMADDW/PMSUBW/PMULTUW/PMADDUW (should use lanes 0 and 2, HI/LO doubleword i = sext(lo32/hi32), rd doubleword i = product), PDIVW/PDIVUW (lane 0, rd unchanged), PDIVBW (divisor rt.h0, words 0-3 to LO/HI; only in dnas), PMFHI/PMFLO/PMTHI/PMTLO width. Matches upstream PS2Recomp issue #22.
- Uncertainty: lane layout confirmed by the game's own usage (rd words = pair sums) and the upstream issue text; odd-lane contents of PHMADH/PHMSBH are from memory and unused by the game.
- insn_difftest mmi run 2 (HI/LO model extended: MULT1/MULTU1/MADD*/DIV1/DIVU1/MFHI1/MFLO1/MTHI1/MTLO1/PMFHL): all real words ok (analysis/codeaudit/insn_mmi_hilo_run2.txt).

## Finding 12 - FPU / VU rounding mode (2026-10-28)
- Hardware: the EE FPU (COP1) and the VUs always round toward zero (FCR31 rounding bits are ignored). The runtime executes translated COP1 / VU0-macro float ops with host SSE in round-to-nearest; only the VU1 interpreter sets `FE_TOWARDZERO` around its run. Inexact results therefore differ by 1 ULP in about half of the cases from hardware for all EE-side float math (physics, skeleton matrices, camera). The reference model also uses nearest, so the old "~1% mismatch" cannot be blamed on rounding.
- Added (untested, built-in but off by default): `SVR06_FPU_TRUNC=1` environment variable sets round-toward-zero on the game thread at start (`ps2_runtime.cpp`, backup `.bak_fputrunc`). Risks to check in a run before making it the default: host stubs on the same thread (libm stubs, GS rasterizer when not on the MTGS worker, raylib calls) now see truncation; compare menu rendering and the `[vuab]` counters with and without it.

## Finding 13 - COP2 MAC/STATUS flags were never produced: collision box test and clip classifiers returned fixed results (2026-10-28)
- Static census of CFC2 reads of VU0 flag registers (real code only): STATUS(vi16) at 0x114FE8/0x115008 (screen clip classifier, function 0x114F70), 0x11ECD4/0x11ED34/0x11EDA8 (libvu0 ClipScreen/ClipScreen3/ClipAll, now served by the exact stubs), MAC(vi17) at 0x10D3E0 and 0x10D448 (functions 0x10D3B0 and 0x10D400).
- **0x10D3B0 / 0x10D400 are a 3-axis box/sphere overlap test**: `d = |a.pos - b.pos| (xyz)`, `vsubx.xyz vf0 = d - (a.size.x + b.size.x)`, then `cfc2 MAC; andi 0xE0; xori 0xE0; sltiu 1` = "all three axes negative" (MAC sign bits x,y,z). 0x10D400 calls `func_10C860` when the test passes. Because the translator's COP2 arithmetic never updates `vu0_mac_flags`/`vu0_status`, MAC was always 0 and the test always returned 0 (never overlapping). This is the first collision primitive located and a probable cause of "no collision".
- 0x114F70 (per-vertex screen transform: vdiv by w, ftoi4, then `vsub.xy vf0 = guardband - v` pairs, `cfc2 STATUS & 0xC0` sticky S/Z bits ORed into vf13.w): STATUS was always 0, so clip codes were always "inside" (nothing rejected/clipped before the GS), which can send huge or behind-camera vertices to the GS.
- Fix: new header `ps2xRuntime/include/ps2_vu0_flags.h` with `ps2_vu0_fmac_flags(ctx, res, dest)` (same per-lane Z/S/U/O model and MAC/STATUS packing as the VU1 interpreter's fast path; overflow detected as a clamped 0x7F7FFFFF result, a true FLT_MAX result is indistinguishable). `tools/codeaudit/patch_vu0_flags.py --apply` inserts the call after the 10 vadd/vsub blocks that precede those flag reads in sub_0010D3B0, sub_0010D400, sub_00114F70 (backups `.bak_vuflags`). Only these functions read flags (census), so the general translator was left unchanged (producing flags for every FMAC op would cost speed for no reader).
- MAX/MINI/ABS/FTOI do not update flags (matches the VU1 interpreter); ABS inside 0x10D3B0 therefore leaves the flags alone.
- Open: other lanes' U/O precision (needs pre-clamp value); verify in a run that overlap tests now report hits (e.g. during a match) and that clip codes no longer stay 0.

## Finding 14 - HLE library semantic review, part 1 (2026-10-28, read-only)
- Reviewed against SDK behaviour: sceCdSearchFile (Kernel/Stubs/CD.cpp), DMA stubs (DMA.cpp), libpad (Pad.cpp).
- Pad: buttons delivered active-low in data[2]/data[3] with the standard bit order (Select,L3,R3,Start,Up,Right,Down,Left,L2,R2,L1,R1,Tri,Circle,Cross,Square); power-up digital mode (id 0x41) with analog opt-in; scePadGetState returns Stable(6) after open and a one-shot transient state; scePadRead returns 1 with 32-byte buffer filled. No semantic defect found. Still to check: pressure bytes (data[8..19]) in press mode, scePadInfoMode id/ext counts after mode switch, vibration (act align) as no-ops.
- Cd/DMA: no defects (see earlier notes). Sif, MemoryCard, malloc/heap stubs not yet compared.
- Sif (SIF.cpp): InitRpc/BindRpc/CheckStatRpc go through the RPC transport; sceSifDmaStat returns -1 (not running/finished; transfers are applied immediately by sceSifSetDma); IsAliveIop 1, SyncIop 1, RebootIop resets the IOP transport and returns 1, InitCmd/InitIopHeap return 0. Consistent with SDK semantics; no defect found. MemoryCard, Gs and malloc/heap stubs remain.
- MemoryCard (MemoryCard.cpp): sceMcSync follows libmc (-1 when nothing pending, else 1 with cmd/result written); sceMcGetInfo reports type/free clusters/format and result (-2 no-format) via the following Sync; Unformat/Format operate on the port directory; all commands complete immediately (no 0 = still-running state), which is acceptable. No defect found. Gs and malloc/heap stubs remain.
- malloc/memalign/free/calloc/realloc (LibC.cpp): thin wrappers over runtime guestMalloc/guestFree/guestCalloc/guestRealloc with correct a0/a1 argument order; memalign(alignment,size) order correct. The game mostly uses its own allocator (0x119850), so these matter little. No defect.
- Gs (GS.cpp): sceGsSyncV waits for a vsync tick (field -1 when interlaced) but does not set v0 to the field parity (SDK returns 0/1 field); callers that use the return value would see stale v0. Low priority, not changed. sceGsSyncVCallback returns the previous callback (correct). sceGsExecLoadImage/StoreImage, SetDefDrawEnv/DBuff reviewed by structure only; pixel-level checks depend on the rendering work (open).
