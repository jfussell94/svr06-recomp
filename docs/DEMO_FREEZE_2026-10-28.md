# Demo freeze: dangling object pointer (2026-10-28 15:00 UTC run)

Symptom: demo starts, a distorted burst of sound plays, sound stops, picture freezes (identical `[shot] litSamples`), game process stays alive.

Evidence (analysis\run_err.txt, first `[guest-branch:missing-target]`):
- Main thread th1 is dead: `st=5 pc=0x000842FF ra=0x0023ADA0`. `0x842FF` is an odd, non-code address.
- Caller is the update method `0x23AC90` at `0x23AD98`: `lw t9,0(s0); lw t9,0x80(t9); jalr t9` (virtual call, vtable slot +0x80), in the countdown loop on `[s0+0x2B8]`.
- `s0 = 0x1973278`, but `s0[0] = 0x434D4946` (bytes `46 49 4D 43`, ASCII "FIMC"), `s0[4]=1`, `s0[8]=0x1972F20`, `s0[0xC]=0x1974360`. This is not an object with a vtable pointer. `[addrinfo]` says the address is not in a live tracked block.
- Call chain at the time (trace): `0x10D070 -> 0x10C960 -> 0x10D110` (repeating) ... `0x4D32C0 -> 0x20CDD0 -> 0x20CCC0 -> 0x20BD80 -> 0x104FC0 -> 0x4D6280 -> 0x4E36B0 -> 0x1DD8A..`.
- Conclusion: the object pointer held by the caller (s0, from a0 in the update loop) points at a data buffer tagged "FIMC" instead of the live object. Candidates: use-after-free/overwrite by the allocator or file-buffer path (`0x1E1440`), or a wrong return value from a recompiled routine.
- Next: find who stored 0x1973278 in the caller's object list (trace `0x10D070/0x10D110`), and whether the real PS2 would have freed/reused this block; check `[gaalloc]`/free ring for 0x1973278.

## Other observations from the same run
- Sound: `[spu2] state` shows ADMA ring peak 18304 only in one burst, then 0 (silent ring) while `adma=1`; `voices=0` always, so no real sample KON (SFX/commentary/crowd) has happened yet. The distorted burst matches the earlier +-18302 clipped ring.
- View-matrix packet `0x1CF97D0` (first vector) is rewritten twice per vsync with small plausible floats (`[matwatch]`, th1 pc 0x126E78/0x126F08, ra 0x14B538/0x14CBD8). It is probably NOT the garbage packet. The garbage uploads (`4a8a486f,4a5de12e,4a5de12e,4e8b08b9`) come from other copies (rdram 0x1E39E70, 0x1E3A220, 0x1E3A4B0, 0x1E3A740, 0x1E3AC10 hold the same first 16 bytes) - still to trace.
- ISS-102 (VDIV/VSQRT/VRSQRT semantics) is already implemented in the translator and generated code (`ps2_vdiv` etc.); changelog entry CL-0043.

## Tooling lessons
- `scripts\fast_rebuild.ps1` copies `svr06-recomp\overrides\*.cpp` over the runner: edit `svr06_overrides.cpp` ONLY in `overrides\`.
- Page-protection write watch (`ps2_pagewatch.cpp`, env `SVR06_PAGEWATCH=1`) crashed the game (AV at svr06run.exe+0x29427cf, 60 s after arming) and no PDB exists for symbolisation; left in the tree but disabled (removed from run.cmd).
- Build script closes a running game before rebuilding (that is not a crash).

## Update 15:45 - dump ram_missing_6b0d4584_0.bin analysed
* Crash this=0x18CD628 (not 0x1973278), ra=0x23ADA0, this[0]='FIMC' again. In the dump 0x18CD628 is a live Node (tag FIMC, prev 0x18CD2D0, next 0x18CE710, owner 0x18CD600, vtable 0x396EE0) and the caller's sub-object this+0x1A0 is zeroed; in the earlier run 0x1973278 held bone matrices.
* Conclusion: the wrestler (Chara, size 0x810) was destroyed and its block reused while a holder of Chara+0x1A0 still ran its update. See YUKES_ENGINE_MAP.md section 5. Owner/free path of the exact block still needs a live free-hook log (no 842ff dump was produced).
