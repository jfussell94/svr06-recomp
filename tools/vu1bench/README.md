# vu1bench: offline VU1 interpreter bench + differential fuzzer (ISS-144, CL-0133)

Runs the game's VU1 interpreter natively on Linux (no game, no Windows): copy `PS2Recomp/ps2xRuntime/include` and
`src/lib/vu/{ps2_vu1_core,ps2_vu1_lower,ps2_vu1_upper}.cpp` + `ps2_vu1_detail.h`, `ps2_vu1_fmac_fast.h`, `ps2_vu1_inline.h` into `rt/`,
put `shim_intrin.h` as `shim/intrin.h`, and the PCSX2 capture files (`vu1MicroMem.bin`, `vu1Memory.bin`,
`PCSX2_Internal_Structures.dat` from analysis/pcsx2_cap/entrance_taker_20261102000300) into `cap/`. Keep a pristine copy in `rt_orig/`.

- `bench.cpp`: runs the 10 hot entrance programs from the capture state; prints cycles, ns per VU cycle and a hash of all registers,
  flags, VU data and every GIF packet. `./bench cap <iters> <debugFlags>` (0 = game default fast path, 512 = exact, 65536 = flag 16).
- `fuzz.cpp`: random programs (FMAC/flags/LQ/SQ/IALU/branches/DIV/XGKICK/I-bit...), one hash per program; `fz.sh` builds it against
  `rt_orig` and `rt` and diffs.
- `mk.sh`: rebuild + compare bench hashes with `base0.txt`, `base512.txt`, `base16.txt` (create them once from the original sources).
- `ir.sh`: callgrind instruction counts (deterministic performance metric).
- `ovh.cpp`: fixed per-run overhead (empty program).
Every CL-0133 change was accepted only with identical bench hashes in all three modes and 0/4000 fuzz differences in modes 0 and 512.
