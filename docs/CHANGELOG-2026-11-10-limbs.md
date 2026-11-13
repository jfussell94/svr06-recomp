## ISS-139 update 11 (2026-11-10 21:45) - in-match limb check while user plays (4-wrestler tag/handicap match, PAL)
- Screenshot bursts (b1..b8, analysis/live/shots) show Triple H holding one crouched pose for ~4 s; skeleton local matrices of all wrestlers DO change between samples (max change 0.6..6.3 over 3-6 s), so the animation is advancing; no frozen data.
- Frame-consistent flag-24 sync dumps taken in the MATCH (sync_ram_0/1: 115- and 132-bone wrestlers): world err 0.000002 / 0.000008, skin err 0.00001 -> skeleton hierarchy and skin matrices are exact in play, not just in entrances.
- Polling monitors (tools/limbmon.py, tools/limbmon2.py): console `ee` length is DECIMAL and capped at 4096 bytes per command (larger replies are silently empty); reads are NOT atomic (one command per ~15 frames), so world!=local x parent 'tear' events and bone jumps from the poll are measurement artefacts (feet/hands move most, so they show most) and prove nothing. limbmon.py (local-matrix jumps only, 84 samples) found no >100 deg jump or non-orthonormal matrix.
- Conclusion so far: no recomp-side skeleton error found in match; misaligned-limb cause (render side: VU1 skin-matrix upload / vertex weights, or hit volumes vs bones) still open. Needs an in-process hook (frame-consistent) at the moment the user sees it.

## ISS-139 update 12 (2026-11-10 22:30) - user: legs/arms clip through mat, hips look over-rotated, grapples/strikes fail to register
- Re-read of same-phase idle pair (analysis/idle_rc.bin recomp, owner 0x18CFA70, channels at [owner+0x20]=0x18D01A0; analysis/idle_pc.bin PCSX2 base 0x1700000, owner 0x18FFA80, channels 0x19001B0). Angles are RADIANS.
  - type-7 (swing/twist) channels: ch6 cur RC (-2.6,0,+2.6) vs PC (+0.7,0,-0.7); ch10 RC (+2.6,0,-2.6) vs PC (-0.6,0,+0.6); ch13 RC (0.1,0,-0.1) vs PC (3.1,0,-3.1); ch17 RC (-0.1,0,0.1) vs PC (-3.0,0,3.0). Every type-7 channel with a non-trivial angle is ~pi off (x-pi, z+pi) => Rx(x)Ry(pi)Rz(z): a 180-deg flip in the middle, scaled by the channel weight (w=+-0.1) -> fits the earlier measured +40 deg thigh-Y over-rotation (b1 46 vs 7, b6 68 vs 17) = "hips rotated too far".
  - Channels 0,1,2 (type 7, zero angles) identical; input matrices +0xB0/+0xF0 match PCSX2 to 0.02-0.05; in RC M1==M2 bit-identical, in PCSX2 M1!=M2 (second matrix = next key?).
- Checked & ruled out this session: 0x10C310 delay slots (neg.s f13/f12 before jal executed correctly), sceVu0RotMatrixX/Y/Z (exact transliterations, bit-verified), libm atan2/acos/asin/pow (recompiled inside sub_00133FD0, not the LibC stubs). NOTE: LibC.cpp stubs atan2/pow read the 2nd float arg from f14 (o32 convention) - EE EABI uses f13; unused by SVR06 today but wrong if ever bound.
- Hit volumes ('ATAC' children) ride on the limbs, so a flipped hip/shoulder swing moves hands/feet and their hit boxes away from where the move expects -> missed grabs/strikes is consistent.
- Next: (A) root cause: same-frame PCSX2 savestate + recomp dump, replay 0x20F940 on identical input and diff step by step; (B) liberty fix behind a flag: post-hook 0x20F940, wrap each type-7 (x,z) pair to the branch with |x|<=pi/2 (x-+pi, z+-pi) / nearest previous frame.

## ISS-139 update 13 (2026-11-10 22:00) - SAME-MOMENT capture (PCSX2 slot 3 + recomp ramdump, Undertaker vs Triple H exhibition, both idle)
- Files: analysis/pcsx2_cap/idle_slot3/eeMemory.bin (PCSX2), build analysis/live/idle_rc1.bin (recomp). Tools: tools/mohocmp.py, tools/skelcmp.py.
- RETRACTS update 12: motion-channel state (all 21 channels incl. the type-7 swing/twist) of both wrestlers MATCHES PCSX2 (owner 0x1976470 vs 0x19A6480 identical to 0.01, owner 0x18CFA70 within 0.13). PCSX2 also has |x|~2.4..3.1 rad; the "pi flip" seen with the old idle pair was a phase difference, not a bug. No angle-wrapping workaround needed.
- REAL DIFFERENCE (skeleton local matrices, recomp 0x18C3B90/0x196B4A0 vs PCSX2 0x18F3BA0/0x199B4B0): every bone matches (median 0.5 deg) EXCEPT the leg chain: thigh bones 1/6 Y = RC 46.6/68.1 vs PC 4.8/15.3 (wrestler A), RC 45.4/57.2 vs PC 0.0/3.0 (wrestler B); knees 3/8 more bent (RC -35/-23 vs PC -22/-26; RC -37/-29 vs PC -8/-9). Feet (4/9), shins (2/7), root (0) identical => a LEG IK solve lands on a different answer (hips swung forward + knees bent = crouch / limbs into the mat). Also a few arm/hand bones (29, 49, 94, 72, 82, 84) differ 37-82 deg => same family (arm IK).
- Writers of thigh local matrix (hw write-watch on bone 1 of 0x196B4A0, 64 writes each per sample): 0x20EE54/5C (ra 0x20EE30, channel apply), 0x2131C4/D8 (ra 0x213184) and 0x213240/54 (ra 0x213200) inside sub_00212190 (0x212190-0x2135C0) = the IK. Its math helpers: 0x10C900 (VU0 vsqrt length), 0x10CF10 (acos via VU0 microprogram VCALLMS 0x280, result vf31), 0x1389C8 -> 0x134F60 __ieee754_atan2f (inside the merged sub_00133FD0), 0x10C310, 0x10BDA0/0x10BD20 rotations.
- Next: hot-plugin probe (overrides/hot/svr06_hot.cpp v3, built OK, backup .bak_v2) checks 0x10CF10 acos, 0x10CDA0 atan2 and 0x1389C8 atan2f against host libm and logs `[mathchk]`. The VU0 acos microprogram at 0x280 has never been verified (only atan2 at 0x78 was, ISS-139 upd 6) - prime suspect.
- The recomp process exited ~1 min after a `watchw 196b630 64` arm/disarm (no crash line in run_err; possibly the hw-watch). Relaunched.

## ISS-139 / CL-0181 (2026-11-10 22:20) - ROOT CAUSE FOUND: VSQI/VSQD translated with fs and it fields swapped (leg IK acos always wrong)
- Probe (hot plugin v3 [mathchk]): VU0-microprogram acos 0x10CF10 returned garbage in the leg IK (ra 0x212C84/0x212D64): acos(0.3748)=2.4e-7 (want 1.187), acos(0.559)=1.571 (want 0.977), acos(-0.19)=2.52 (want 1.76). atan2 0x10CDA0 and 0x1389C8 were fine except the 0,0 corner.
- Cause: VSQI fs,(it++): fs = bits 11-15 (rd), it = bits 16-20 (rt). CodeGenerator::translateVU_VSQI/VSQD used vi=rd, vf=rt. For `vsqi $vf1,($vi0++)` (0x10CF48 in acos, 0x10CEE4 in sub_0010CED0) it stored vf0 at vi1*16 and bumped vi1 instead of storing the argument at VU0 mem 0 -> the microprogram ran on stale data. vsqi $vf1,($vi1++) (atan2, 0x10CCA0) has equal fields so it hid the bug.
- Fix: ps2xRecomp/src/lib/vu_translation_helpers.cpp (.bak_vsqi) fixed fields, and vi0 never increments; generated sub_0010CF10 and sub_0010CED0 patched by hand in PS2Recomp and PS2Recomp_usa (.bak_vsqi). Hot probe left in place (v3) to confirm acos matches after the rebuild.
- Expected: leg IK solves like PCSX2 (thighs ~5 deg not 46-68), no crouch/mat clipping; hips, hit volumes and grapple range line up. Possibly also arm IK (bones 29/49/72/82/84/94).
- Note: PS2Recomp_usa generated VU0 code uses addr without the 0x11004000 base (older generator) - USA VU0 macro memory accesses need a regen/audit.

## Update 14 - VSQI fix verified by probe (2026-11-11)
- Hot probe: `acos10CF10 calls=50000 bad=0` after the VSQI/VSQD operand fix (previously mismatching). Leg-IK acos now matches std::acos.
- `atan2f_1389C8` "bad" counts are a probe artefact: 0x1389C8 tail-jumps (`j 0x134F60`); dispatchGuestBranch returns false for non-call jumps, so the outer dispatcher runs the target after the hook returns. The hook read f0 before the real function ran. Not a recompiler bug.
- Pending: in-match skeleton capture + skelcmp vs analysis/pcsx2_cap/idle_slot3 to confirm thighs ~5 deg (was 46-68), mat clipping gone, grabs/strikes registering.

## Update 15 - user-verified (2026-11-10 22:23 CT)
- User confirmed in-game: limb/leg misalignment fixed after the VSQI/VSQD operand fix (CL-0181).
- Follow-ups: USA build needs the same patch + heap fix and a VU0 macro-address audit; remove mathchk probe; mirror CL-0173..0181 to Project docs.

## Update 16 - match profile + native atan2 (2026-11-10 22:35 CT)
- In-match (PAL, 2 wrestlers): fps 59-60. VU1 interpreter = ~2.0 s of every 3 s (~67% of a core, ~23 ns/cycle, ~300k runs/3 s). VU0 microprogram calls ~18.8k/s at ~5 us = ~9% of a core.
- atan2 wrapper 0x10CDA0 was the bulk of VU0 calls (3.1M of 11.6M). The only mismatches vs libm are (0,0) -> game returns pi/4. Replaced with a native hot hook (`overrides/hot/svr06_hot.cpp` v4, backup .bak_v3): VU0 calls/s 18.8k -> 12.8k. Not yet in the permanent overrides.
- Next: VU1 hot programs (0x28b8, 0xe60, 0x2638) native compile / flag-16 no-flag path; remaining VU0 users (0x10C900 length etc.).

## CL-0182 - match speed: game thread was CPU-bound; uncached getenv + lazy flags (2026-11-10 23:00 CT)
- Real speed metric: guest vsync/s (heartbeat `vsync=`), NOT the `fps=` field (that is host presents, always ~60). In-ring match: ~16.7-17.9 vsync/s (PAL target 50). Matches ISS-158 (~18).
- Game process: ~1.1 of 12 logical cores busy; one thread (GameThread) saturated.
- Hot-plugin stack sampler (overrides/hot/svr06_hot.cpp v6/v7, real RtlVirtualUnwind, 3000 samples): ~16% of GameThread samples in ucrtbase called from exe+B5CAE69 = `std::getenv("SVR06_IMGW")` evaluated on EVERY GIF packet in gs_vulkan_bridge.cpp (lines 609, 638; ~83k packets/s; Windows getenv locks + scans the environment).
- Fix: gs_vulkan_bridge.cpp (.bak_getenv) caches it in a function-local static; EeScheduler.cpp (.bak_getenv) same for SVR06_EEDUMP per vsync.
- Re-applied CL-0146 (VU1 lazy exact flags + FIFO commit) from the *.cl0146 copies: ISS-160 showed CL-0146 was NOT the boot-stall cause; current files were byte-identical to the pre-CL-0146 baseline (.bak_lazyflags), backups .bak_prereapply. Offline: bit-identical on 10 hot programs + 18k fuzz, bench ~26 -> ~17 ns/VU cycle. Disable at runtime with debug flag 31.
- Game's own memcpy (0x119340) measured: ~1.8k calls / 0.4 MB / 3-4 ms per 3 s - negligible.
- Debug flag 16 (skip flag bookkeeping) alone: 16.7 -> 17.9 vsync/s.
- Native atan2 (hot plugin v4+): VU0 calls 18.8k/s -> 12.8k/s.

## Update 17 - launcher + trackers (2026-11-10 23:15 CT)
- Launcher build pill: `build_state()` (tools/launcher/server.py, .bak_cards) now also reads analysis/build_only_log.txt (the non-interactive/assistant PAL build) when newer than build_log.txt, recognises `BUILD END ... exit=N`, and reports "linking" near the last files. Previously rebuilds started outside the launcher never showed in the pill.
- Launcher memory-card picker (server.py .bak_cards, ui/app.js .bak_cardsel): named cards live in `<boot-file folder>/cards/<name>`; `mc0` is a directory junction (mklink /J, no admin) to the active card, re-pointed on selection / before Play. Existing `mc0` becomes `cards/Default`; old `mc0_backup_*` folders become "Backup <time>" cards. "New card" creates an empty one. Disabled while the game runs. Nothing is deleted. Untested on Windows (junction code path) - first Play after selecting a card should confirm.
- Launcher cheat list: "Any gender can win any title (Exhibition)" status untested -> working (user-verified 2026-11-10; stale "needs 2026-11-09 build" text removed). GM Mode version still todo.
- Completion trackers re-scored: tools/game_completion.json (.bak_1110) -> docs/GAME_COMPLETION.md weighted 37% -> 42%; docs/COMPLETION_TRACKING.md headline ~97% intro/title, ~82% playable, area table re-estimated; Performance corrected to 35% (game ~17 of ~50 vsync/s in-ring).
- Build (CL-0182: getenv cache + CL-0146 lazy flags) was linking at 23:13 (clang-cl LTO link > 17 min CPU); not yet tested in game.

## Update 18 - launcher Settings: memory card format (2026-11-10 23:30 CT)
- Settings page has a "Memory card format" card: Folder (native, works today) or PCSX2 card file (.ps2). prefs `card_format` / `card_file` (server.py .bak_cardfmt, ui/app.js .bak_cardfmt); build_env exports SVR06_MCFORMAT / SVR06_MCFILE.
- The runtime (Kernel/Stubs/MemoryCard.cpp) only supports host-folder cards, so the PCSX2 choice is stored and shown with a "not supported by the game yet" note; Play still uses the folder card. TODO: .ps2 card image reader/writer in MemoryCard.cpp (backup before write), per requirements-and-plan.md.
