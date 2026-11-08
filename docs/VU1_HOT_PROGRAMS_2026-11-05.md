# VU1 hot programs, static analysis (ISS-144, 2026-11-05)
Generated offline by `tools/vu1_progstat.py` from the PCSX2 entrance capture (`analysis/pcsx2_cap/entrance_taker_20261102000300/vu1MicroMem.bin`). No game run needed.

## Findings
- The ten hottest entrance programs are tiny: 21-72 instruction pairs each (35f8 is the exception at 181). Each has one E-bit exit and 1-3 loops, and none uses JR/JALR except 35f8 (2 indirect jumps).
- **None of them reads a flag register** (no FMAND/FSAND/FCAND/FMEQ/FSEQ/FCEQ...) except 35f8 (FCAND x10, FMAND x1, with its own CLIP x6). So MAC/status/clip bookkeeping in the fast path is wasted work for the other nine, which are ~85% of VU1 cycles, unless a LATER program reads stale flags written here (needs a runtime A/B check before relying on it).
- 28b8 (39% of cycles, ~103k runs per 3 s at ~200 cycles) has 60 pairs, 3 back-edges, 2 DIV, 13 LQ-type and 10 SQ-type ops, 8 ITOF0 / 4 FTOI0: a vertex transform/convert loop. e60 (17%) and the 5a0/a0/a00 family are MULAx/MADDAy/MADDAz chains (matrix x vector). 2638/2570/2580/2590 (all end at 0x2638) are the XGKICK packet writers (MUL/ADDA/MADDA + FTOI4 + SQI x4).
- Per-run cost at 33-37 ns per VU cycle is ~5-7x slower than a lean interpreter would need; the programs are small and mostly straight-line, which is the ideal case for ahead-of-time compilation (hctp-style `vu1_recomp`).

## Raw output
```
== start 0x28b8: 60 pairs reachable (0x2320..0x28c7), 1 E-bit exits, 3 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 2 0 0 | LQ/SQ: 13 10
   upper top: NOP=8, ITOF0=8, MULAx=6, MADDAy=6, MULAz=5, MADDx=5, MADDz=4, FTOI0=4
   lower top: NOP=26, LQ=9, SQ=6, LQI=4, SQI=4, MR32=2, DIV=2, IBNE=2
== start 0xe60: 23 pairs reachable (0xe60..0xf17), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 9 2
   upper top: NOP=10, MULAx=4, MADDAy=4, MADDAz=2, MADDw=2, MADDz=1
   lower top: LQ=5, IADDIU=4, LQI=4, NOP=4, ILW=2, SQI=2, IADDI=1, IBNE=1
== start 0x2638: 31 pairs reachable (0x2550..0x2647), 1 E-bit exits, 2 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 1 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 9 4
   upper top: NOP=20, MUL=2, ADDA=2, MADDA=2, MADD=2, ADDi=1, FTOI4=1, MINI=1
   lower top: LQ=7, SQI=4, ILW=3, IADDIU=3, NOP=3, LQI=2, IADD=2, B=1
== start 0x22b0: 72 pairs reachable (0x22b0..0x24ef), 1 E-bit exits, 2 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 2 0 0 | LQ/SQ: 20 10
   upper top: NOP=13, MULAx=8, MADDAy=8, ITOF0=8, MULAz=5, MADDx=5, MADDz=4, FTOI0=4
   lower top: NOP=27, LQ=13, LQI=7, SQ=6, IADDIU=5, SQI=4, ILW=2, DIV=2
== start 0x35f8: 181 pairs reachable (0x3060..0x3607), 1 E-bit exits, 2 back-edges, 2 indirect jumps
   flag-reader/writer lower ops: {'FCAND': 10, 'FMAND': 1} | CLIP: 6 | XGKICK: 1 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 39 13
   upper top: NOP=117, MULw=10, MADD=6, CLIP=6, ITOF0=6, SUB=3, ADDw=3, MUL=3
   lower top: LQ=39, NOP=37, ILW=13, IADDIU=13, SQI=13, FCAND=10, IADD=10, IADDI=9
== start 0x5a0: 56 pairs reachable (0x5a0..0x75f), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 1 1 0 | LQ/SQ: 16 2
   upper top: NOP=20, MULAx=9, MADDAy=9, MADDz=5, MADDAz=4, MADDw=4, MUL=1, ADDy=1
   lower top: NOP=23, LQ=12, IADDIU=5, LQI=4, ILW=3, SQI=2, IBNE=2, WAITQ=2
== start 0x22c8: 69 pairs reachable (0x22c8..0x24ef), 1 E-bit exits, 2 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 2 0 0 | LQ/SQ: 17 10
   upper top: NOP=10, MULAx=8, MADDAy=8, ITOF0=8, MULAz=5, MADDx=5, MADDz=4, FTOI0=4
   lower top: NOP=27, LQ=10, LQI=7, SQ=6, IADDIU=5, SQI=4, ILW=2, DIV=2
== start 0x2570: 25 pairs reachable (0x2570..0x2637), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 1 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 8 4
   upper top: NOP=14, MUL=2, ADDA=2, MADDA=2, MADD=2, ADDi=1, FTOI4=1, MINI=1
   lower top: LQ=6, SQI=4, NOP=3, LQI=2, IADDIU=2, IADD=2, IMM=1, IAND=1
== start 0xa0: 61 pairs reachable (0xa0..0x287), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 1 1 0 | LQ/SQ: 30 2
   upper top: NOP=16, MULAx=11, MADDAy=11, MADDAz=7, MADDw=7, MADDz=4, MUL=1, ADDy=1
   lower top: LQ=26, NOP=16, LQI=4, ILW=3, IADDIU=3, SQ=2, IBNE=2, WAITQ=2
== start 0xa00: 49 pairs reachable (0xa00..0xb87), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 0 | DIV/SQRT/RSQRT: 1 1 0 | LQ/SQ: 12 2
   upper top: NOP=22, MULAx=7, MADDAy=5, MADDAz=3, MADDw=3, MADDz=2, MADDy=2, MUL=1
   lower top: NOP=22, LQ=11, ILW=3, IADDIU=3, IBNE=2, SQI=2, WAITQ=2, LQI=1
== start 0x2590: 21 pairs reachable (0x2590..0x2637), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 1 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 6 4
   upper top: NOP=11, MUL=2, ADDA=2, MADDA=2, MADD=2, FTOI4=1, MINI=1
   lower top: LQ=6, SQI=4, IADDIU=2, IADD=2, NOP=2, IAND=1, ILW=1, IADDI=1
== start 0x2580: 23 pairs reachable (0x2580..0x2637), 1 E-bit exits, 1 back-edges, 0 indirect jumps
   flag-reader/writer lower ops: none | CLIP: 0 | XGKICK: 1 | DIV/SQRT/RSQRT: 0 0 0 | LQ/SQ: 6 4
   upper top: NOP=12, MUL=2, ADDA=2, MADDA=2, MADD=2, ADDi=1, FTOI4=1, MINI=1
   lower top: LQ=6, SQI=4, NOP=3, IADDIU=2, IADD=2, IMM=1, IAND=1, ILW=1
```
