# Graphics findings (GS, VU1, VIF, rasterizer, textures)

Written 2026-10-27. No game assets are reproduced here; only formats, register values and addresses.

## 1. How the native renderer works today
- The GS is emulated in software: `PS2Recomp/ps2xRuntime/src/lib/gs/` (`gs_frontend.cpp` parses GIF packets; `gs_cpu_backend.cpp` rasterizes; `ps2_gs_memory.cpp` addresses VRAM; `ps2_gif_arbiter.cpp`).
- The finished frame is displayed through raylib/OpenGL. It is neither a Vulkan nor a Direct3D 12 renderer. A GPU backend (Vulkan first, software GS kept as the reference/fallback) is planned for performance, Linux, Steam Deck and Android support.
- VU1 is a software interpreter (`lib/vu/ps2_vu1_core.cpp`, `_lower.cpp`, `_upper.cpp`, `ps2_vu1_detail.h`) fed by a VIF1 packet interpreter (`lib/ps2_vif1_interpreter.cpp`). Both GS and VU1 run synchronously on the game thread; that is the main performance limit (FPS 52-59, `vu1run` about 2 s of every 3 s in one profile).
- EE-side VU0 macro mode (COP2) is translated to SSE by the recompiler (`ps2xRecomp/src/lib/vu_translator.cpp`, `vu_translation_helpers.cpp`) with runtime helpers in `ps2_runtime_macros.h`.
- Display: 640x448 default (`kDefaultDisplayWidth/Height`), host frame buffer 640x512. PAL game logic runs at 50 Hz; native presents at 60 FPS.

## 2. GS register decoding facts used by the tools
- TEST: ATE bit0; ATST bits1-3 (0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER, 7 NOTEQUAL); AREF bits4-11; AFAIL bits12-13 (0 KEEP, 1 FB_ONLY, 2 ZB_ONLY, 3 RGB_ONLY); DATE bit14; DATM bit15; ZTE bit16; ZTST bits17-18. `0x5000d` = alpha test on, GREATER, ref 0, KEEP on fail, Z test GEQUAL.
- PSM codes: 0 CT32, 1 CT24, 2 CT16, 10 CT16S, 19 T8, 20 T4, 27 T8H, 36 T4HL, 44 T4HH, 48 Z32 (and Z24/Z16/Z16S).
- ALPHA `0x44` = (Cs - Cd) * As >> 7 + Cd (A=0 source, B=1 dest, C=0 source alpha, D=1 dest), the standard "alpha blend".
- CLUT: 16-entry (4-bit) CT32 CLUT in CSM1 occupies an 8x2 pixel area at `cbp`; entry n is at pixel index n with address bits 3 and 4 swapped (`swizzleClutIndexCSM1`). The native `m_clut` stores CT32 entries split: low half at `[i]`, high half at `[i+256]`.
- `TEX0.CLD` semantics implemented in `GSCpuBackend::LoadClut`: 0/6/7 no load, 1 always, 2/3 load and set cbp0/1, 4/5 load only if cbp differs from the remembered one.

## 3. Text rendering (title, tips, menus)
- Glyphs are drawn as textured SPRITEs from an atlas in PSMT4HL (psm 36 = 4-bit indices stored in the upper nibble of 32-bit words, bits 24-27) at `tbp0 0x1800`, `tbw 4`, 256x256; the lower 24 bits of the same words belong to another image (RGB), a typical trick. Palette: 16-entry CT32 CLUT at `cbp 0x1810`.
- The palette is white with an alpha ramp from 0x00 (index 0, background) to 0x80 (index 15); two entries are black with partial alpha (drop-shadow/outline). The same atlas is also used with other CLUTs (a green/yellow set was seen at the first use).
- Draw state: TEX0 TCC=1, TFX=0 (modulate), TEST `0x5000d`, ABE on with ALPHA `0x44`. Because index 0 has alpha 0 and the alpha test is GREATER 0, background texels must not be drawn.
- Native shows a halftone box around each glyph (ISS-103). VRAM contents and palette are correct (verified by decoding the GS dump with a standalone script) and the run-time check `[glyphdbg]` (CL-0025) confirmed that the rasterizer's `m_clut` and texture reads are correct during a 20-tick window (no transparent texel came out visible, no cache/direct mismatch). The HUD/pill pieces that share the glyph pass state are drawn with garbage vertex positions (A-03), so the boxes are probably a symptom of the garbage-matrix problem; re-check after that is fixed.
- Interaction to remember: the shared 32-bit pages mean any CT24/CT32 write (with FBMSK/PSM confusion) into the same pages can corrupt the nibble atlas; the check in `[glyphdbg]` compares the cached read with a direct VRAM read.

## 4. PCSX2 vs native comparison (11 user screenshots, 2026-10-27)
Matches PCSX2: title background and logo, demo match geometry (wrestlers, ring, crowd, cage), 60 FPS.
Differences:
1. Halftone box around every glyph (title "Press START button", tip screens) - ISS-103.
2. Blue/red "Press START" pill bar missing; demo bottom banner/HUD band missing (only faint "Press Any button" text) - ISS-104.
3. Loading and tip screens covered by large orange/yellow/green streaks: the 3D scene is drawn with a garbage camera matrix - ISS-101.
4. Cage centre panels drawn as washed-out bands - ISS-105.
5. Freeze after the match loads - ISS-100.

## 5. VU1 microcode findings (from `[vudump]` snapshots)
- Programs observed: pc `0` = skinned transform (bone matrices at VU1 data qword `0x321`, stride 4 qwords; loop count/base at `0x320` .x/.y); view matrix at `0x3f4..0x3f7`; projection/screen constants `0x3f8..0x3ff`. pc `0xe60` and pc `0x22b0` are other transform/clip programs.
- Double-buffer offsets seen: TOP `0x280` / `0x2d2` qwords.
- In the loading/tip frames these matrices contain garbage, which explains the streaks, the collapsed 3D scene (all vertices land on one screen point) and the missing/misplaced HUD and pill pieces. Measured on 24 snapshots (2026-10-27): `0x3f4..0x3f7` identical in every snapshot (stale), values are integers times 2^32; `0x321` contains an all-NaN pattern. The two candidate causes are listed in ISS-101 (bad source data vs. EE-side VU0 math) and analysed in `findings/analyses.md` A-03 and A-06. `[vifmat]` logs the UNPACKs that write that block.
- GS pass order of a loading/tip frame: 3D scene strips (degenerate), HUD strips T8 512x64 / T8 128x128 / T4 64x16 with ALPHA `0x44` and TEST `0x5000d` (garbage coordinates), glyph sprites (T4HL), 512-wide copy sprites (CT32 `tbp 15808`, TEST `0x53001`, FRAME `0x400000064`).
- Tools: `~/vu/vudis.py vu1_code.bin <startbyte hex> <count>` (minimal VU1 disassembler in the VM); `analysis\vu1_code.bin` and `analysis\vu1_data_NN.bin` (16-byte header {startPC, top, itop, dataSize} + 16 KB data) are written by the `[vudump]` block while the GS dump window is active.

## 6. GS stream facts
- PCSX2's per-frame header is a libgs `sceGsDBuffDc` packet: 22 A+D writes (FRAME fbp 0 / 0x50 alternating, XYOFFSET with field offset 0x7800/0x7808, SCISSOR 640x256, a clear sprite with TEST `0x30000`). The native stream lacks it because libgs is HLE-stubbed through TOML stubs (`sceGs*@0x11xxxx`) although recompiled originals exist (`sub_0011D840` ...). Plan: restore the originals at run time (`replaceFunction`) so real packets flow through DMA/GIF. (Status: planned, not done.)
- Framebuffers: FBP 0 and 0x50 (blocks), FBW 10 (640 px). `vramdump` + a PSMCT32 de-swizzle script gives approximate pictures.
- Dump tooling: env `SVR06_GSDUMPTICK` / `SVR06_GSDUMPLEN` set a window of vsync ticks; `analysis\recomp_gs_7000.bin` + `recomp_vram_7000.bin` (cwd = build dir) hold the packets and VRAM. `gifdump2.py` / `gifdump3.py` print per-draw state (PRIM abe/fst, FRAME, ALPHA, TEST, CLAMP, XYOFFSET, TEX0 incl. psm names, TSTRIP/TFAN). Usage: `python3 gifdump3.py $HOME/gsd.bin`.

## 7. Resolved red herrings (do not repeat)
- Zero-size quad object `0x644EB0` (vtbl `0x37CD00`) is layout item '001' of the ATB0 layout whose record has (w,h)=(0,0) by data; per-frame apply `0x115FE0` calls `setSize`. Degenerate quads are expected and not the cause of the garble.
- The black screen after the title movie (2026-10-26) was a missing-overlay issue, not a GS issue.
- The game's own screenshot capture returns black; do not use it. Use `computer_screenshot` or the VRAM dump.

## 8. GS correctness notes found while reading the backend
- `applyTexa` handles CT24/CT16 TEXA (AEM, TA0, TA1); CT32 passes through. `LookupCLUT` applies TEXA to the CLUT entry by CPSM.
- The 8-bit CSM1 CLUT index remap (`csm==0 && 8-bit`): `block = min((idx & 0xF0) + clutBase, 240)`; verify against PCSX2 when 8-bit CLUT textures show wrong colours.
- `wrapTextureCoordinate` implements REPEAT/CLAMP/REGION_CLAMP/REGION_REPEAT; region modes use MINU/MAXU/MINV/MAXV from CLAMP.
- Bilinear filtering is applied after the CLUT lookup (RGBA lerp), which is the correct order for indexed textures.
- Dithering (DTHE) and the 16-bit alpha threshold (`encodeFramePixelPSMCT16`: alpha >= 0x40 -> bit 15) are emulated; there is no per-pixel dither matrix check yet.

## 9. Planned graphics work
1. Fix ISS-103/104/105 (paletted alpha / blend).
2. Fix garbage matrices (ISS-101/102/100).
3. Replace the HLE libgs with the original code so the GS packet stream matches PCSX2.
4. GPU backend (Vulkan), async GS/VU1, resolution scale, widescreen/4:3 correct scaling, texture packs up to 8K (hash-based replacement keyed by TEX0 + CLUT), Linux/Steam Deck/Android/low-end PCs. The software GS stays as the 1:1 reference for verification against PCSX2 GS dumps.
