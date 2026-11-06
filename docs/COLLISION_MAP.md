# Collision map (static tracing, 2026-10-29)

## sub_0010D7B0 — `collidePair(out*, A*, B*)` (EE main code)
- Args: a0=out (4 bytes written as one float store), a1=A, a2=B. A and B are "collision groups": +0x00 int child count, +0x08 pointer to array of child pointers, +0x10 group bounding volume (16-byte-aligned block).
- Step 1: `func_10D3B0(A+0x10, B+0x10)` = 3-axis group bounds overlap test (the routine whose MAC flags were previously never produced). If no overlap: out = 0.0f (all bytes zero).
- Step 2: for each child of A (skip child when float at child+0xC == 1.0f, i.e. disabled/inactive): for each child of B (same skip, and skip when child+0x14 & 8): if `func_10D4A0(...)` (quick child test) passes then `func_10D400(Achild,...)` (narrow overlap) must also pass.
- Result byte layout (written at sp+0x7C then loaded as float into *out): byte0 = 1 (hit), byte1 = A child index, byte2 = B child index, byte3 = 0.
- Callers seen: overlays ovxGame/ovlD functions 0x4D4900/0x4D4920 (jal func_10D400 directly) and 0x4D7320 (calls both 10D3B0 and 10D400). Match-time collision lives in the Game overlay bank.

## Open
- Response (what happens after a hit: knockback, grapple trigger) not traced; callers of 0x10D7B0 not yet listed (needs a full-runner search, ~45 s per rg on this mount).
- Verify in a match run that out byte0 becomes 1 for wrestler-vs-wrestler contact.

## Call chain (found 2026-10-29)
- sub_00228F70 (recursive tree/list walk: calls itself at 0x2290dc; also 10EA80, 1198A0/1198B0, 10BF10, 11E230, 10DB10, 10CBE0, 104D80, 1050C0, 2F6CA0) -> sub_00229530 (only caller of 0x10D7B0) -> sub_0010D7B0 collidePair -> 10D3B0 / 10D4A0 / 10D400.
- sub_00229530: calls func_229330, then collidePair, then func_229370; on a hit it calls a virtual method (jalr t9 at 0x2296ac / 0x22978c / 0x229824) = collision response via vtable; also 1050C0, 119800, 2293E0.
- Next: identify the vtable slot targets at those jalr sites at run time (log t9 once) and whether the response runs during a match.

## Run finding (2026-10-29, demo match)
- Manager at 0x62BAC0 (object list: count +0x3C, pointers +0x40..): `sub_00229500(mgr,obj)` = add (append), the first function at 0x229530 = remove (find+compact), the loop at 0x2295B0 = update-all (outer i loop; skips an object when float at (obj+0x1C)+0x20 == 0; inner j loop calls collidePair).
- In the demo the manager had count=5, and the five objects have tag word 0x43415441 ('ATAC') at +0 and a block at obj+0x1C whose +0x24.. holds Japanese UTF-8 text; this may be a valid class tag + name string rather than stale memory (UNCONFIRMED, treat 'stale' as a hypothesis), f20 == 0 for all => every pair skipped, collidePair 0x10D7B0 never called, no hit response. Same family as the Chara stale-update bug (arena wipe without destructors, ISS-102): the list is never cleared / re-registered for the new scene's objects.
- Next: ADD/REMOVE logging (`[collmgr] ADD/REMOVE`) to find who registers and when the count should be reset.

## CORRECTION + verification (2026-10-29, demo match run with add/pair logs)
- The manager objects are NOT stale. Objects carry tag 'ATAC' (attack-collision hitbox objects), registered through `0x229500` ADD from `0x22925C` (inside the 0x228F70 walk): 5 ADDs per scene, no REMOVEs. The float at (obj+0x1C)+0x20 is the "active" value: 0 when the wrestler is not attacking, so all pairs are skipped on idle frames (that explained the earlier zero calls).
- Verified in the demo: `collidePair` is called (`[collpair] calls=100`) and returns a real hit: `HIT #1 A=01974720 B=018CC800 res=00010101` (byte0=1 hit, A child 1, B child 1). The response virtual call at 0x2296AC fires: `[collresp] site=0x2296ac t9=005F9C40 a0=01974550` (target is a Game-overlay function at 0x5F9C40, this = attacker's ATAC object).
- Result: the overlap test (MAC-flag fix), pair query and response dispatch all work in the recompiled game.
- Remaining: trace 0x5F9C40 (hit response: damage/knockback/reaction state), the second response site 0x22978C, the overlay direct callers 0x4D4900/0x4D7320 (body-to-body), ring/rope bounds.

## Hit-response function 0x5F9C40 (MainGame overlay, ovxMainGame_005F9A40, frame 0x130)
- Signature (observed): a0 = attacker ATAC object (this; s1), a1 = ?, a2 = target key passed to `0x1050A0`, a3 = s4 (kept). s3 = *(this+0x1B8) = the attacker's move/state block.
- Resolves the defender via `0x1050C0(0x1050A0(a2), 'CHAR' (0x43484152))` (find-by-tag lookup) -> s2 = *(found+0x10) = Chara. Bails out (returns 1) if s2 == 0.
- Branches on `*(s2+0x49C)` (non-zero skips ahead) and the attacker's move id at `*(s3+0xB4)` (special-cased ids 0xDC, 0x35, 0xEA), then calls 0x4E2740 / 0x4E2830 / 0x4E3E10 (Game-bank helpers) and reads Chara fields 0x1DC (state == 6 test), 0xBC.
- Next: trace the move-id dispatch and the damage/reaction writes (Chara +0x1DC state field) to document the hit-reaction state machine.

## Run data: hit responses (2026-10-29, demo, `[hitresp]` log in overlay MainGame 0x5F9C40)
- Gotcha: runner overlay files are overwritten from `svr06-recomp/overrides/` on every rebuild, so any patch to an ovx*/ovl* runner file must be made in `overrides/` (backup `.bak_hitlog`), not only in `PS2Recomp/.../runner/`.
- Observed hits are symmetric pairs (attacker<->defender each get a response call): `[collpair] HIT res=00000F01` (A child 0xF? byte layout [hit, A idx, B idx]) followed by `[hitresp] this=<attacker ATAC> a2=<defender ATAC> a3=01FFFCFC (stack out) moveid=220 (0xDC) or 53 (0x35)`; ra=0x2296B4 (site 0x2296AC). Both observed move ids (0xDC, 0x35) are exactly the ids special-cased at the top of the handler, so the handler's main branches are exercised.
- Collision status 40% -> 50% (hits, response, real move ids verified); remaining: what 0xDC/0x35 are (likely idle/walk/strike hitbox states), damage/reaction writes, ring bounds.

## Handler 0x5F9C40 structure notes
- Early branch: attacker move id (*(this+0x1B8)+0xB4) in {0xDC, 0x35, 0xEA} -> test defender state `*(Chara+0x1DC) == 6`; if equal call `0x502BE0(s3, chara)` / `0x502BB0(s3, chara)` (Game-bank predicates) and return 1 when they fail; otherwise fall into the long path (~1600 instructions, up to ~0x5FB000) which looks up child objects by tag via `0x1050C0(obj+0x1C0, tag)` with tags 'ATAC' (0x43415441) and 0x00434850, tests flag bits at Chara+0x188 (bit 2) and +0x174, and writes reaction state. Fully reversing it is a separate task (entrances/game flow area).

## Handler 0x5F9C40 structure (static, 2026-10-29)
- s1 = this (attacker ATAC obj), s3 = *(this+0x1B8) = attack record (move id at +0xB4, flags at +0x174 / bit at +0x188), s2 = defender Chara found by `1050C0(1050A0(a2), 'CHAR')`; bail (return 1) if none; `*(s2+0x49C) != 0` shortcut.
- Move ids 0xDC, 0x35, 0xEA all branch to 0x5F9D5C: tests defender state `*(s2+0x1DC) == 6` and then calls `0x502BE0(s3, s2)` / `0x502BB0(s3, s2)` (move-class vs defender checks). Later: looks up 'ATAC' (0x43415441) and 'CHP' (0x43485000-ish tag) children of the defender (`s2+0x1C0`), tests bit 0x188 flags, calls 0x1DD8A0 / 0x4E3590 (team/side compare), 0x2430F0, decrements counters at defender +0x2EC / clears +0x3A0, then 0x4E1E80 (can-be-hit test for both), move id 0x33 special case, 0x10BC30 (distance/angle check using float at gp-0x7BFC) -> reaction setup.
- Interpretation: this is the "is this hit valid / which reaction" gate (team, state 6, move class, range) before damage/reaction start; reactions themselves are in the helpers it calls.
