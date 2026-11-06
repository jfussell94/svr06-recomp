# SVR06 port progress (separate trackers)

Updated 2026-10-28. Estimates by the assistant; each line states its basis. Update this file, then run tools/progress.py.

| Area | % | Basis |
|---|---|---|
| Intro / title screen | 98% #########+ | boots, logos, title at a locked 60 FPS (CL-0112/0114); PRESS START pill over the logo/stadium picture after the title movie (CL-0115); open: the game's own IPU stadium loop (ISS-122, approximated by the held logo frame), glyph drop-shadows over video |
| Overall playable | 73% #######... | menus and demo run; match logic not verified |
| Game-code audit: libvu0 math (30 routines) | 95% #########. | transpiled from the game's own code, bit-exact vs reference model; built (0 errors), not yet verified in a run |
| Game-code audit: COP2 translator (per-instruction) | 90% #########. | DIV/SQRT/RSQRT exact; ftoi clamp; MAC/STATUS flag production added for every flag reader (collision overlap test 0x10D3B0/0x10D400, clip classifier 0x114F70; libvu0 clip routines via exact stubs); U/O precision and exp-0xFF input handling pending |
| Game-code audit: FPU (COP1) translator | 100% ########## | bit-exact with REAL PS2 hardware: 901/901 ps2autotests ee_fpu cases (tools/codeaudit/hwfpu_check.py); all 2246 testable COP1 words in every code image x 60 random states = 0 mismatches vs the hardware model (ee_fpu_model.py); SQRT.S operand bug fixed (4 sites, CL-0110). Residual: the ACC sticky-overflow input of MADD/MSUB (hardware keeps ACC when ACC itself overflowed earlier) is not modelled; no hardware case exercises it |
| Game-code audit: integer/MMI translator | 100% ########## | every ALU/MMI/HI-LO op class audited; word multiply/divide (PMULTW/PMADDW/PMSUBW/PMULTUW/PMADDUW/PDIVW/PDIVUW/PDIVBW) and PMFHI/PMFLO/PMTHI/PMTLO rewritten, 20 cases x 400 random states match the model (mmiword_test.py); game census: no code uses the word forms |
| Game-code audit: other HLE-replaced libraries (sceGs/Dma/Cd/Pad/Sif...) | 65% ######.... | sceIo, libm, ret0/ret1 triaged; Cd, DMA, Pad, Sif, Mc, malloc reviewed (no defects); Gs reviewed structurally (SyncV return value missing, minor); pixel-level checks pending rendering work |
| Skeleton / pose / animation logic | 40% ####...... | skeleton, bone records, euler->matrix, VU0 trig/matrix programs, motion banks mapped; keyframe playback not yet |
| Collision logic | 15% #......... | math layer audited; first collision primitive found (3-axis overlap test 0x10D3B0/0x10D400, was always false because MAC flags were never produced, now fixed); callers/broadphase and response not yet mapped |
| Entrances / game flow logic | 16% #......... | resource namespace catalogued; per-wrestler resource kind table (/BELT /CENT /RELF /SIDE /NAPL) and path builders mapped; state machines not yet traced |
| Music / soundtrack logic | 35% ###+...... | music ctor + ADX mapped; song name/album text records found in the menu text database (65 slots 000-064, 057-064 are unused "BGM NAME 0xx" placeholders); list-building code still unmapped |
| Memory map / address documentation | 50% #####..... | MEMMAP.md + class vtable catalogue (177) + resource namespace |
| Rendering (Vulkan / paraLLEl-GS, default) | 75% #######+.. | black title/after-movie fixed (remembered DISPFB candidate, CL-0062); 100 min run cycles intro/title/demo with a lit picture; sizes verified from dumps; open: movie composite check, OpenGL removal (ISS-120) |
| Rendering (software GS) | 60% ######.... | draws scenes; garbage geometry and halftone issues open |
| Performance / multi-core | 55% #####+.... | Vulkan present 20 ms -> 1-2 ms per frame (no wait_idle, readback/downscale outside the GS lock, CL-0114); intros, title, memory-card screens and loading locked at 60 FPS, game vblank clock at a full 60/s; matches/menus with heavy VU1 still to measure |
| Audio output | 40% ####...... | partial |
| Input (XInput / keyboard) | 30% ###....... | keyboard works; XInput pending |
| Memory card / saves | 30% ###....... | partial |
| Windowing (resize/widescreen/4:3) | 25% ##........ | pause-on-drag bug open |
- 2026-10-29: COP2 normal-value mismatches 0 (CL-0068/0069); rebuild pending.
