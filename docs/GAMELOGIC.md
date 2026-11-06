# SVR06 game logic map (from static analysis of the ELF; no emulator code involved)

## Skeleton / pose  (main ELF)
- `0x1BEC50 skeleton_init(skel, boneDefs, boneCount)`: stores bone count at +0xAC and bone-definition pointer at +0xA8; allocates (via 0x119850 alloc + 0x11B190 memset) three arrays of `count*64+16` bytes -> +0xA0 local matrices, +0xA4 scratch matrices, +0xB0 world matrices (4x4 float, 64 B each), plus a pointer array at +0xB4 filled by `0x1BF630` (one 0x50-byte node per bone).
- Bone definition record (0x50 bytes): +0x10..+0x18 translation xyz, +0x20 rotation (euler) -> `0x10C960` builds the 3x3/4x4 rotation, +0x30 parent index (-1 = root; error string "bone error![%s]" at 0x360DD0 if invalid), +0x38 bit0 = has-parent flag.
- World matrix = local matrix * parent world matrix, done inline with VU0 macro ops (`vmulax / vmadday / vmaddaz / vmaddw`, 4 rows) at 0x1BEE34-0x1BEEA4, i.e. it depends on exact COP2 ACC/madd behaviour in the translated code.
- Motion files are loaded by `0x1DFFE0` from `/MOT/<NAME>` and `/MOTP/<NAME>` (CAGE, BRAP, TAG, GM55-57, LADD, RUMB, CASK, MINI, TABL, SLEP, GAME, WCAM, M3DA, MSTG, WEAP). Bone name strings: bone01.. at 0x36A7C8.
- Math core (VU0 microcode): chain of 10 MPG packets at ELF 0x30C58C.. (see CODEAUDIT.md finding 5); programs called with `vcallms` from 0x10B000-0x10D300.

## Still to map
Collision, entrances/state machines, music selection, global tables (see docs/PROGRESS.md).

## Motion bank loader  (main ELF)
- `0x1DFFE0` (2032 bytes) is the motion-set initialiser: allocates 0x38/0x40-byte bank objects and calls `0x1E1E90(bank, "/MOT/<SET>", n)` for each set (`/MOT/CAGE`, `/MOTP/CAGE`, ... see list above). Which sets load depends on the match-settings struct returned by `0x1DD8A0` (bytes +0x09, +0x14, +0x2F select match type/arena/cage/ladder etc.). Globals: `$gp-0x6934` holds the motion manager (fields +0x14, +0x1C zeroed at start).
- `0x1E16F0(obj, 0)`: constructs the default bank; `0x1E0AC0` (816 B), `0x1E0E50`, `0x1E0FB0` are the per-bank load/lookup helpers (not yet analysed).

## Sound / music (main ELF)
- `0x1C9DB0` Sound-stream base-class ctor: vtable 0x37CEB0, 0x2C-byte voice slots (count at +0x24 and +0x2C), opens `bgm/BGM999.afs` via the ADX (CRI ADX/PS2EE 9.42, statically linked) layer; derived class ctor at `0x1CF8A0` (vtable 0x37CFE0, 0x70-byte object, helper `0x1D0D10`).
- Movies use `moviep/NMV999.afs`.
- No rand/LCG constants found in main or overlays (checked glibc, MSVC, Numerical Recipes, 69069, MT) other than a PCG-style constant in overlay `dnas`; song choice therefore uses either a timer-based seed or a table-driven rotation. Lead for the "same song every time" bug: check which timer/counter register the song picker reads and whether our runtime returns a constant for it.

### Randomness lead (runtime side)
- Our libc `rand/srand` stubs are time-seeded (Kernel/Stubs/LibC.cpp:1085) and `sceCdReadClock` returns the real RTC, so those cannot cause identical choices.
- EE timers (0x10000000/0x800/0x1000/0x1800) are cycle-driven from reset (`PS2Memory::advanceEeTimers`), i.e. fully deterministic run to run. If the game seeds the song/loading-graphic pick from a timer count (cop0 Count is read only in libc at 0x2E9C6C/0x2E9D88), the pick repeats each launch. Planned fix (when builds are allowed): start the timer counters / cop0 Count from a wall-clock-derived offset at reset.

## VU0 microprograms (tool: tools/codeaudit/vudis.py, listing in analysis/codeaudit/vu0_programs.txt - local only, derived from game code)
| micro addr | role (from disassembly) |
|---|---|
| 0x000 | atan helper: loads 5 polynomial coefficient quads (vf20-28 from VU mem 4..), computes ratio with `div Q` then branches to the polynomial at 0x0F8 |
| 0x078 | atan2 front end: |x|,|y| -> (|y|-|x|)/(|y|+|x|) reduction, then polynomial |
| 0x0F8 | odd-power Taylor/minimax polynomial in the reduced ratio (shared atan tail; uses ACC madd chain) |
| 0x280 | asin/acos: `rsqrt Q, x, 1-x^2`, then ratio + shared polynomial |
| 0x510 | (26 insns) not yet classified |
| 0x600 | (39 insns) not yet classified |
| 0x760 | (38 insns) not yet classified |
| 0x890 | 3x3 adjugate / cofactor builder (cross products of the two input rows vf1,vf2 -> rows vf28-30, I = -1); called by `vcallms 0x890` from the pose code at 0x10BEA0 |
| 0xAB0 | (28 insns) not yet classified |
These programs rely on exact `div/rsqrt` (Q register), ACC madd chains and the I register - the same VU interpreter paths that were previously inexact in the translated COP2 code, so the engine's angle and matrix math (and with it wrestler orientation and anything keyed off angles, such as collision facing tests) depends on them.

## Resource namespace (PAC virtual paths found in the main ELF; xrefs in analysis/codeaudit/xrefs.tsv)
- /ADX/:  CHER ANNO RR SPRF REFM
- /AUTO/:  SDB FLAG FNT2
- /BELT/:  %04x
- /CARD/:  ICON
- /CBAT/:  DATA NPFT MENU
- /CENT/:  %04x
- /CHAR/:  PRO DAT PRO
- /CSDT/:  R%03d TENG TITA TKOR TJAP MAKE
- /EDIT/:  FACE MOT PRTS BLOD
- /EMD/:  %d
- /EMD2/:  %d
- /EMD3/:  %d
- /GA2D/:  BASE
- /JEWL/:  %04d
- /MENU/:  FNT2
- /MISC/:  %d GPRM
- /MOT/:  CAGE BRAP TAG GM55 GM56 GM57 LADD RUMB CASK MINI TABL SLEP GAME WCAM M3DA MSTG WEAP
- /MOTP/:  CAGE BRAP TAG GM55 GM56 GM57 LADD RUMB CASK MINI TABL SLEP GAME M3DA WEAP
- /MOV/:  M000 M001 M002 M003 M004 M005 M006 M007 M008 M009 M010 M011 M012 M013 M014 M015 M016 M017 M018 M019
- /NAPL/:  %04x
- /NETD/:  IRX
- /NLDA/:  %04d
- /PBP2/:  DATA
- /PCCH/:  %04d
- /PCEN/:  %04d
- /PCGE/:  GAME ST%02d
- /PCGI/:  %04d
- /RELF/:  %04x
- /SDB/:  MENU GAME MOVE GMRS DNAS MING
- /SFX/:  B1MN B1NO B2NO B1CB B2CB B3CB B1CL B2CL B1CG B2CG B1BS B2BS B3BS B1PK B2PK B3PK B1BR B2BR B3BR B1FF B2FF B1CK B2CK B3CK B1EN B2EN B1TL B2TL B1CW B1KW B1SC B2SC LIST
- /SIDE/:  %04x
- /SOBJ/:  %03d
- /STBL/:  DEFS
- /STG/:  %02d CAGE CAGL CELL LADR LADU LADC CASK FFYF BORD WPON
- /TIPS/:  %04d
- /TMBC/:  %04d
- /TMBD/:  %02x%02x
- /TMBM/:  %04d
- /TMCP/:  %02x%02x
- /TMJW/:  %04d
- /TMMT/:  %04d
- /TMNP/:  %02x%02x
- /TMRP/:  %02x%02x
- /TMSP/:  %02x%02x
- /VOIC/:  IRX

Notable: /CENT/%04x (per-character entrance data), /CHAR/PRO + /CHAR/DAT (wrestler profile/data), /STBL/DEFS (state table definitions), /ADX/CHER (crowd cheer) /ADX/ANNO (announcer) /ADX/REFM (referee) /ADX/SPRF, /SFX/* (sound-effect banks incl. B?EN = entrance), /PBP2/DATA. Class vtable catalogue: analysis/codeaudit/ptrtables.txt (177 function-pointer tables, 0x326178-0x381000).

### Change made (untested, needs next build)
`PS2Memory::readIORegister` timer-count reads now add a per-launch constant offset (random_device + steady_clock; shifted by 3 bits per timer index) so timer-derived seeds differ between launches while all deltas stay identical. Backup: ps2_memory.cpp.bak_timerjit. Verify after the next build that the song and loading picture vary; if not, the picker uses another source (next candidates: vsync counter, sceCdReadClock value, pad frame count).

## Math helpers used by the pose code (main ELF)
- `0x10C960 euler_to_matrix(mat4 *out, const float xyz[4])`: wraps each of the three angles into [-pi, pi] using gp-relative constants (-0x7FAC=0?, -0x7F88/-0x7F84/-0x7F80 = +-2pi / -pi / pi style), clears `out` to identity (vmr32 chain), calls `0x10D110` (per-component sin) and `0x10D070` (per-component cos) on the angle vector into two temp vectors, then combines them with COP2 multiply/add chains into the rotation matrix. All of it depends on exact COP1 compare/add/sub and COP2 mul/madd behaviour.
- `0x1BEC50` (see above) calls it once per bone with the bone's euler triple at record +0x20 and stores the result into the local-matrix array (+0xA0 + 64*i), and the world matrix (+0xB0) is local * parent world.

## Motion bank class
- `0x1E1E90 MotionBank::MotionBank(this, name, setIndex)`: base init `0x1E1340`, vtable 0x37D330, +0x38 = set index, resolves the PAC file for `name` (`0x119B30` + loader `0x1E1440(this, name, handle, 0)`).
- Registration helper (0x1E1EE8..): looks the file name up (`0x1035F0` then `0x103A80`), writes the bank pointer into the global table at `*(gp-0x6934) + 4*setIndex` and calls virtual slot 2 (+8) on the bank; `0x1DFF00` is the retry/wait path. Destructor with vtable reset at 0x1E1F50 region (`0x1E13B0` base dtor).

## Collision primitives (main ELF 0x10D340-0x10DBC0; listing analysis/codeaudit/collision_region_10d340.txt)
- Volume record (16-byte vectors): +0x00 centre xyz (w used as scratch/1.0), +0x10 .x = half extent (cube). 
- `0x10D340 merge(a, b)`: builds the enclosing cube of two volumes: min/max of (centre -/+ extent) per axis, new centre = (min+max)/2 (0.5 constant), new extent = max over axes of half the span; stores result in `a`, saving/restoring the w-lane/+0x10 value.
- `0x10D3B0 overlap(a, b)`: `|a.c - b.c| < a.ext + b.ext` on all three axes (MAC sign bits 0xE0) -> returns 1/0. `0x10D400 overlap_then(a, b)`: same test, then calls `0x10C860` (58 callers; a vector helper) on success. Callers: main 0x10D7E4 / 0x10D8B4 (broadphase wrappers), overlay game 0x4D737C, 0x4D4AAC, 0x4D74E4, 0x4D73D0 (match collision code), main 0x10D654 (merge).
- These depend on COP2 MAC flags, which the translator never produced before 2026-10-28 (see CODEAUDIT finding 13).
- Match collision (overlay `game`): object (wrestler/character) holds its collision volume at object+0x30 (centre xyz at +0x30, half-extent at +0x40). `0x4D7320(objA, objB)` first runs `0x10D3B0(A+0x30, B+0x30)` and bails out with 0 when the volumes do not overlap; otherwise it continues with the detailed hit test (float math, uses gp-relative constants). `0x4D4A..` (loop over an object list, flags at +0x14: bit0/bit2 skip, +0x1C -> sub-object with type field +0x40 == 5) calls `0x10D400(obj+0x30, other)` then `0x10EB50` (contact generation) and a per-object virtual callback at +0xB0. Listing: analysis/codeaudit/collision_callers_ovlgame.txt.

## Entrance / per-wrestler resource tables (main ELF)
- Resource kind table at 0x36C220, 8-byte entries {path format, callback tag}: 0 `/BELT/%04x` `cb_b`, 1 `/CENT/%04x` `cb_c` (entrance), 2 `/RELF/%04x` `cb_r`, 3 `/SIDE/%04x` `cb_s`, 4 `/NAPL/%04x` `cb_np` (further entries `cb_j000..` follow at 0x36C248). Table read via `lui 0x37; addiu -0x3DE0`.
- `0x25BAE0 / 0x25BB10 / 0x25BB30` are sprintf helpers (`func_2F9240`) building the per-wrestler path from (kind, id); `0x25BB40` returns the tag string for a kind; `0x25BB80(out, kind, mode, idx, ...)` is the resource-key builder (calls `0x259D00` to resolve the numeric id for the wrestler, mode 0/1 selects the sprintf variant). `0x25C8AC` is another caller (display list / vertex table builder, object +0x58/+0x5C counters).
- So an entrance for wrestler N is the PAC entry `/CENT/<id as %04x>` loaded through this table; /SIDE and /NAPL are side (partner/manager) and name-plate resources. Listing: analysis/codeaudit/resource_loader_25ba00.txt.

## Music selection - static dead end (2026-10-28)
- The main ELF contains no song-title strings; only `bgm/BGM999.afs` (0x33EE80, one xref at 0x1C9DB0). Titles/indices therefore live in PAC/overlay data or are read from the AFS itself, so the selection table cannot be mapped from the ELF alone; it needs a match-state/menu dump or a run to see which index the picker passes to the stream object (derived ctor 0x1CF8A0).

## Update 2026-10-28 (engine research; see YUKES_ENGINE_MAP.md)
* Object ABI: vtable -> {offset_to_top, rtti, fn0...}; virtual k at +8+4k; secondary-base thunks subtract the base offset. Node base (vtable 0x37C190): tag@0, flag@4, prev@8, next@0xC, owner@0x10, ownsChildren@0x14, vtable@0x18.
* Chara (wrestler) = 0x810 bytes; ctor 0x2398F0, dtor 0x239B40, update 0x23AC90 via thunk 0x23CC30 (this+0x1A0); bases at +0x1A0/+0x1B0/+0x1D8.
* Allocator: arenas (table 0x3BC030, current arena [gp-0x6B24]); scenes call 0x20AB10 -> 0x119BF0 which wipes arenas without destructors. Layouts are now code constants in `overrides/svr06_engine.h`.

## Update 2026-10-29 — resource entry constructor (entrance/resource path)
- `0x25C550(this, loader, kind, mode, idx, ..., t0, t1, t2)` constructs a per-wrestler resource entry: vtable 0x37EFB0 (lui 0x38 / -0x1050), fields +0x28 kind (index into table 0x36C220), +0x2C mode, +0x30 loader/owner, +0x34 data block (allocated via 0x119850, size slot count<<7 + 0x10, filled via 0x11B190), +0x38 name copy (0x20 bytes via 0x119340), +0x58/+0x5C/+0x60 counters (slot counts: 1, or 9 when mode != 1). Builds the resource key with `0x25BB80`. Only caller of 0x25BB80 is this constructor; its callers are in `0x25D860` (two sites 0x25D9CC / 0x25DA30).
- Next: trace `0x25D860` (resource manager / loader queue) to find who requests kind 1 (/CENT) and the match-state that triggers entrances.
- `0x25D860` (resource entry factory: allocs via 0x119800, helpers 0x25BEA0 / 0x25BC30, virtual calls on the entry) is called from `0x25DD00` (site 0x25E11C) and `0x259280` (site 0x25949C). Those two are the next functions to read (resource manager level).
- Resource-level callers (2026-10-29): `0x25DD00` (2139-line recompiled function; calls the factory 0x25D860, 0x25D620, 0x25D580, 0x25A880, 0x230B60/230670/2304C0, 106EE0) is called from main-ELF 0x2695C0, 0x269300, 0x260220 and from Menu overlay functions 0x55AE70 / 0x54B9C0, so wrestler resource loading is triggered by menu selection code as well as match setup. `0x259280` (uses 0x25A9B0 x4, 0x25D860, 0x25D620, 0x2594E0) is called from 0x1DDDC0.

## Update 2026-10-29 — music selection verified across runs
- `[music] StartAfs handle=3150A0 ptid=1 fid=<n>` logs the game's song pick. Over 5 logged runs fid was 5, 13, 1, 13, 1 (title theme is BGM1 entry 1 and is started separately) so the pick now varies run to run (the timer-seed fix works). All values are valid indices into `BGM/BGM1.AFS` (17 entries, 0..16, 8-17 MB each = full songs); other banks: BGM2.AFS 40 entries, BGM3.AFS 27 entries (3-4.7 MB), BGM1ENVB/H (17 env variants), BGM0.AFS 7 (SFX-length).
- Native ADX decode path plays the picked entry (`decoding ...BGM1.AFS entry 5 (adpcm_adx 44100 Hz 2 ch)`).
- Still open: which table maps match/arena to ptid/fid (the picker passes ptid=1 each time so far: BGM1 is the entrance/match-song bank), and a PCSX2 comparison of the distribution.
