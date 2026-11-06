# Detailed analyses (investigation notes)

Written 2026-10-27. Each entry: what was observed, method, evidence, what was ruled out, current conclusion, next step. Ids `A-nn` are referenced from the issue register.

---

## A-01 Heap exhaustion at match load (solved)
Observed: arena scene froze; main thread spun in the game's `free()` (`0x11A410`, loop `0x11A47C`); `new` returned NULL for small sizes (8, 0xF0, 0x390, 0xBA4, later 0x19C4, 0x650, 0x80).
Method: live RAM dump + stack walk; heap integrity watch (hooks at `0x119E70` and `0x11A410`) reporting concurrent entry and the first call after which arena 15 chains became inconsistent; call ring buffer; `[livereport]` of live blocks grouped by caller at first NULL.
Evidence: arena 15 (`0x62A180-0x16F37F0`, 17.4 MB) held the 14 MB GAME motion block and was exactly full; the free list linked to zero-size "used" blocks (looked like corruption but followed from the failed allocation); the runtime cap `0x01F00000` was about 0.96 MB under the real console's `0x1FF0000`.
Ruled out: a race (symptom varied but the cap was deterministic); a codegen bug in the allocator.
Conclusion/fix: ISS-009 / CL-0021. Side effect: a PAC/BPE decompression into a failed buffer produced size `0x22A807FC` and `memcpy(NULL, ..., 32 MB clamp)` wiped RAM in one run, which is why the first symptoms looked random.

## A-02 RAM-wide lane-3 bit-31 corruption after the match asset load (open)
Observed: after the 14 MB motion load and the PAC resource load, bit 31 is set in word 3 of each 16-byte quadword across all RAM; examples: scene pointer `0x39A6DC` `00738220` -> `80738220`, mainCb `801BFC70`. The main loop dies, which is the freeze users see after the match loads.
Method: canary poll (300 us) on `0x31618C | 0x39A6DC | 0x3B5E7C` bit 31; on fire it prints `vs` tick, the three values, four snapshots of every EE thread (pc/ra/entry/sp) and three full-RAM passes counting lane-3 bit-31 qwords (count/first/last) 40 ms apart.
Evidence so far: pattern is lane-specific and RAM-wide; it appears right after large loads; the canary has not yet fired in runs with the new instrumentation (runs ended before the load or the corruption did not recur).
Hypotheses (ranked): (1) a 128-bit store loop (memset/memcpy style using MMI such as PCPYLD/PCPYUD/POR/PEXT*) with a wrong macro implementation; (2) a VIF/GIF/DMA path (VIF1 UNPACK V4-32 with mask/mode or SPR/IPU transfer) writing wrong lanes over a huge range; (3) a COP2 VU0 macro op such as `VABS/VNEG/VMOVE`-family writing back through `SQC2` with a mask error; (4) a runtime HLE copy (CD read, BPE/PAC decode override) using 128-bit helpers with a sign-bit pattern.
Test to discriminate: when it fires, read first/last offender (is it the whole 32 MB or a sub-range?), then look at the thread pcs; disassemble the loop; check whether the offender range equals a destination of the 14 MB load.
Related: the VU1 garbage matrices (A-03) may have the same cause (a sign bit in lane 3 of matrix rows is exactly "w negative").

## A-03 Garbage camera/bone matrices in VU1 data (open)
Observed: PCSX2 shows correct loading and tip screens; native shows orange/yellow/green streaks (3D scene drawn with bad matrices).
Method: `[vudump]` writes `analysis\vu1_code.bin` once and `vu1_data_NN.bin` snapshots (24 max) while the GS dump window is active; `vudis.py` disassembles; the loop count/base at qword `0x320`, bone matrices at `0x321` (stride 4), view at `0x3f4-0x3f7`, projection/screen at `0x3f8-0x3ff`.
Evidence: matrices contain garbage; the VU1 program itself is the normal skinned transform. 24 snapshots in one 20-tick window (loading/tip frame, 2026-10-27): qwords `0x3f4..0x3f7` are byte-identical in every snapshot (stale, never refreshed) and hold exact integers times 2^32 (e.g. 7500.0*2^32 = `55ea6000`, -21728.0*2^32 = `d6a9c000`; bit 28 set in many words; rows 1 and 2 of each qword pair are equal), plus `0,0,0,-47630.36`; qword `0x321` (first bone matrix) changes from random finite values to the all-NaN pattern `ffffffff 7fffffff ffffffff ffffffff`; the loop count at `0x320` is 6..9 with `.z = 255.0`; the constants at `0x3f8/0x3f9` (0.16,0.16,0.2,1.0 / 0.35,0.35,0.35,0.0) are sane. The 3D loading-scene TSTRIPs collapse to a single point (bbox x 208, y -1522); the HUD pieces get coordinates of about -1600..+2100 px.
Interpretation: `0x3f4..0x3f7` is persistent (uploaded once per scene and kept across programs) and was either never uploaded or uploaded from garbage; `0x321+` bone matrices come from per-object packets and contain NaN bit patterns typical of an all-ones fill or of a masked/compare result (SSE compare masks are `ffffffff`, `abs` of that is `7fffffff`), which the PS2 VU would have clamped.
Hypotheses: (1) the source matrices in RAM are garbage because of A-02 or uninitialised data; (2) EE-side VU0 math (matrix concatenation/inverse/normalise via COP2) produces NaN/Inf or wrong lanes (see A-06).
Next: `[vifmat]` (CL-0027) prints the guest source address of every UNPACK that targets qwords `0x3f4..0x3f7`; read those RAM addresses with the live console (`ee ADDR LEN`), then watch them for writes (heap-watch style hook) and find the EE function that fills them. Compare good (demo) vs bad (loading) uploads from the same log. Check also whether UNPACK formats with fewer than 4 components (V2/V3) leave stale lanes: the interpreter copies the old destination lanes (`lanes[]`) for the components it does not write, whereas hardware writes defined values (check against PCSX2 `VIFunpack` for V3-xx W handling and for STMOD/row/col masking); a wrong mode here would leave garbage in W lanes.

## A-04 Glyph box (paletted text) (open, instrumented)
Observed: halftone box around every glyph in native; PCSX2 clean.
Method: decode the GS dump VRAM with a standalone script (Python) using the PSMCT32 block/column tables; confirm texture index data and CLUT.
Evidence: T4HL atlas `tbp0 0x1800 tbw 4`: index histogram is dominated by 0 (55,912 of 65,536) and glyph shapes decode correctly; CLUT `cbp 0x1810` (CT32, 8x2 layout) is white with alpha ramp 0x00..0x80 (index 0 alpha 0) with two black shadow entries. GS TEST `0x5000d` (alpha > 0 passes). Code review of `passesAlphaTest`, `classifyAlphaTest`, `LookupCLUT`, `LoadClutUnlocked`, `combineTexture`, bilinear path: no bug found. `[texlog]` first-use CLUT at the same cbp was green (so the CLUT is reloaded between uses).
Run-time check (CL-0025, 20-tick GS dump window on a loading/tip frame, 2026-10-27): the first glyph sample showed `m_clut` entry 0 alpha 0, entry 1 alpha 0x0c, entry 15 alpha 0x80 (correct), TEST `0x5000d`, ALPHA `0x44`; there were NO samples with index 0 and non-zero alpha and NO mismatches between the cached texture read and a direct VRAM read. Texture data, CLUT load and sampling are therefore correct for the glyph pass in that window.
Pass order in that frame (from `gifdump3.py`): the 3D scene TSTRIPs (collapsed to one point, see A-03), then three 2-vertex TSTRIPs with the SAME state as the glyphs (ALPHA `0x44`, TEST `0x5000d`) and textures T8 512x64 (`tbp 16000`), T8 128x128 (`tbp 15936`), T4 64x16 (`tbp 16128`) with bbox about -1600..+2100 px ("HUGE"), then 16 glyph sprites (T4HL, bbox x 204..386 y 435..469), then the 512-wide copy sprites. So the HUD/pill pieces are being transformed with garbage and fall outside the screen or smear across it.
Conclusion: the glyph boxes are most likely a symptom of the garbage-matrix family (A-03); re-check them after A-03 is fixed and then diff the glyph pass against PCSX2 if they persist.

## A-05 GS pass analysis of the title frame (partial)
Observed passes: background/logo (matches PCSX2); text glyph sprites (T4HL, ABE `0x44`, TEST `0x5000d`); a blue/red pill bar missing in native; frame header (libgs DBuff) missing in native because libgs is stubbed (see graphics findings section 6).
Next: list every draw in the title frame with `gifdump3.py` from both a native dump and a PCSX2 GS dump (GS dump via PCSX2 `Ctrl+F8`-style capture, replay with GSdx dump tools) and diff PRIM/TEX0/TEST/ALPHA/FRAME per draw.

## A-06 VU0 / COP2 macro-mode translator audit (done by code reading; fixes pending)
Scope: `ps2xRecomp/src/lib/vu_translator.cpp`, `vu_translation_helpers.cpp`, `ps2xRuntime/include/ps2_runtime_macros.h`.
Findings:
1. ADD/SUB/MUL/MADD/MSUB (and the accumulating forms) go through `PS2_VADD/VSUB/VMUL` = `ps2_vfinish/ps2_vslow`, which flush exponent-0 results and fall back to a double path on exponent 0xFF. Correct enough: no NaN/Inf escapes.
2. `VDIV`: emitted as `Q = (ft != 0) ? fs/ft : 0`. Hardware: ft==0 (or denormal) -> `Q = ((fs ^ ft) & 0x80000000) | 0x7F7FFFFF`; other results clamped to +-FLT_MAX; D/I status flags set. Mismatch.
3. `VSQRT`: emitted `sqrtf(max(0, ft))`. Hardware `sqrt(|ft|)` (invalid flag for negatives). Mismatch.
4. `VRSQRT`: emitted `ft > 0 ? 1/sqrtf(ft) : 0` and IGNORES `fs`. Hardware `Q = fs / sqrt(|ft|)`, ft==0 -> `sign(fs) | 0x7F7FFFFF`. Mismatch (wrong whenever `fs` is not 1.0, e.g. normalising with a scale).
5. `VMAX/VMINI` use `_mm_max_ps/_mm_min_ps` (NaN handling differs from VU, which has no NaN; low risk).
6. `PS2_VDIV/PS2_VSQRT/PS2_VRSQRT` macros in the header are unused by the translator and lack clamping; they should be rewritten or removed when the translator is fixed.
7. Status flags (MAC/CLIP/STATUS) and `VCLIPw` results matter for games that branch on them via `CFC2`; translator support is partial (`VCLIP` is emitted inline).
Fix plan (CL-0030): emit calls to new header helpers `ps2_vu_div/ps2_vu_sqrt/ps2_vu_rsqrt` that implement the PCSX2 semantics, then regenerate every function that contains COP2 DIV/SQRT/RSQRT (find them by grepping the MIPS comments for `vdiv|vsqrt|vrsqrt`; scan one directory at a time, do not use a recursive grep over the runner), including overlay banks. Header edits are now allowed by the user.
Expectation: removes NaN/Inf and wrong-length vectors from matrix/normalise code but may not explain A-03 alone.

## A-07 Overlay and entry-point completeness (done)
Method for finding missing entries: list every `jal`/`j` target in the main ELF and in every overlay; compare against `register_functions.cpp` / bank tables; every target inside the main-ELF range that is not a function start needs an `entry_points` line (ISS-006). For overlays, targets must also be collected from the overlays of the other slot (ISS-007). Hidden starts (leaf functions only reached through vtables) are found by scanning for 16-byte aligned code after `jr ra`+nop and for lui/addiu code pointers (ISS-008).

## A-08 Why `[watchdog] STUCK? running=0` is not a hang
Per frame the main thread sleeps in SleepThread (`0x126F00`) and is woken by the vsync callback chain; at the instant the watchdog samples, no thread is Running. A real stall shows Dormant threads or no vsyncTick advance.
