# Yuke's engine map (reverse-engineered from SLES_536.76) - 2026-10-28

Tools: `tools/codeaudit/vtables.py` (515 vtables, 147 matched to constructors -> `analysis/codeaudit/vtables.tsv`),
`tools/codeaudit/newsites.py` (3890 `new(size)` sites with ctor -> `analysis/codeaudit/newsites.tsv`), plus existing
`callers.py`, `disasm.py`, `ptrtables.py`. Method: static only (no game changes).

## 1. Object model
* C++ compiled with a GCC-2.9x/EE-gcc style multiple-inheritance ABI. A vtable pointer points at `{offset_to_top(0), rtti(0), fn0, fn1, ...}`
  so virtual function k lives at `vtable + 8 + 4*k`. Calls: `lw t9,0(this); lw t9,off(t9); jalr t9` (no delta add at the call site).
* Secondary bases get their own vtable pointer inside the object and **thunks** that do `a0 -= base_offset; j real_fn`
  (example `0x23CC30: a0 -= 0x1A0; j 0x23AC90`). A pointer to a secondary base is therefore *not* the object address.
* Constructors store vtable addresses with `lui/addiu` then `sw reg,0(base)`; derived ctors first run base ctors
  (writing the base vtable) then overwrite with their own - `vtables.py` uses that to link vtable -> ctor.

## 2. Node base class (the engine's task/scene tree) - vtable 0x37C190, ctors 0x104D30 / 0x104D80, dtor 0x104DD0
```
+0x00 tag     4CC name or 0 (0x104D80 stores a1, 0x104D30 stores 0)      e.g. 'Csmc' (0x4363736D)
+0x04 flag    1 (enabled)
+0x08 prev    previous sibling (set by append 0x104EC0: parent.next=child; child.prev=parent)
+0x0C next    first child / next sibling
+0x10 owner   the object that embeds this node (used by thunks / walkers)
+0x14 owns    1 = destroy children on dtor (0x104E70 calls vtable(+0x18)[slot 2](child,1) for each)
+0x18 vtable
```
`0x104EC0` append-at-tail, `0x104F00` insert-after, `0x104F30` unlink, `0x104E70` destroy-children. Any engine object that is a
Node has a **4CC tag at +0 instead of a vtable**. The `'FIMC'` value seen at the crash site (`46 49 4D 43`) is such a tag, not a data buffer.

## 3. Character ("Chara") object - size 0x810
* Allocated in the spawn routine `0x2389bc` (`new(0x810)`), constructed by `0x2398F0`.
* Primary vtable 0x37EAE0; secondary vtable pointers at `+0x1A0` (0x37EAF4, update thunk 0x23CC30 -> 0x23AC90), `+0x1B0` (0x37EB04),
  `+0x1D8` (0x37EB14, long table: update 0x23AC90 is slot 12). Node sub-object at `+0x1B0` (owner = the Chara).
* Destructor 0x239B40 (thunks 0x23CC40/60/70): resets vtables, releases 3 children at `+0x7E0..0x7E8`, `+0x7EC`, `+0x7F0`, flag `+0x7F8`.
* Per-frame `0x23AC90(this)`: runs when `byte[this+0x188]&1`, uses `+0x804` (anim ptr), `+0x1C0` sub-object, copies pose rows
  `+0x100..0x120`, counter `+0x2B8`, virtual slot `+0x80` (0x23AD98) when the counter expires.
* Bone/pose output is a float4x4 array in the heap (seen at 0x1972F20 / 0x1973278 in the earlier dump).

## 4. Allocator
`new` 0x119800 / 0x119850 (size in a0), `delete` 0x1198A0 (flag in a1 of dtor: `>0` means also free), arena table 0x3AC030, 16-byte block headers.
Large buffers: 0x1E1440. Most gameplay classes are created in the form `new(size); if (p) ctor(p,...)`.

## 5. The demo/exhibition freeze (ISS-102) - what the model says
* Crash site: `jalr` at 0x23AD98 inside Chara update with `this=0x18CD628` (this run) / `0x1973278` (earlier run), `this[0]='FIMC'`.
* In the new dump (`ram_missing_6b0d4584_0.bin`): `0x18CD628` is now a **live Node** (tag FIMC, prev 0x18CD2D0, next 0x18CE710, owner 0x18CD600,
  vtable 0x396EE0), and the caller's sub-object `this+0x1A0` (0x18CD7C8) is zeroed. In the earlier run the same address range held a
  **bone-matrix array**. So the Chara was *destroyed and its block reused* while something still held `Chara+0x1A0`.
* Therefore the bug class is **use-after-free / teardown-vs-update ordering**, not a bad vtable or a codegen error in 0x23AC90.
* Open: who still holds the `+0x1A0` pointer (the caller of thunk 0x23CC30, vtable 0x37EAF4 slot 1) and why the owner list was not unlinked
  (Node dtor 0x104DD0 -> unlink 0x104F30) or was updated after teardown. A candidate for the real cause is thread ordering: the match is torn
  down by one EE thread while the main update thread still runs (our scheduler is cooperative, the PS2 kernel is priority-preemptive).

## 6. Next RE steps (no behaviour changes yet)
1. Find callers that load `vtable+0xC` of a `+0x1A0` pointer (update list) and the list head they walk.
2. Map teardown: callers of dtor thunks 0x23CC40/50/60/70 and of `delete` for size 0x810; which thread calls them.
3. Same method for sound (class 0x1C9DB0/0x1CF8A0, sid 0x300/0x301 packets) and movie objects (`mvobj` at 0x87D0A0).
4. When the game is next run: log (not change) `0x239B40` entry (thread id, this) and `0x23AC90` entry; the first update after a destroy proves the race.

## 7. Whole-binary classification (second pass)
Tools: `tools/codeaudit/modules.py` (clusters all 9,831 main-ELF functions: call graph minus 110 hub utilities, shared-global edges via `$gp=0x3A0A70` and lui/addiu data refs, vtable-sibling edges, address adjacency) -> `analysis/codeaudit/modules.tsv`, `modules_roles.tsv`, `modules_summary.md`;
`tools/codeaudit/classify.py` (tiered evidence V/C/S/G/A -> `classified.tsv`).
* Coverage: **9,831 / 9,831 functions assigned to a module (100%)** in 1,342 modules; vtable-slot members 1,151; constructors from `new` sites 307 (3,890 `new` sites total in all images).
* Honest scope: this is structural classification, **not matching decompilation**. Only 15 modules carry a semantic anchor (Node, alloc/free, file-buffer, Chara, skeleton, motion banks, collision, sound-stream, movie, ADX, cdrom, resource, obb-test). The rest have fingerprint roles only (VU0-math, bulk-copy, float-logic, integer state machines). The Yuke's game code has almost no strings (stripped), so naming needs per-module reading; this is the remaining RE backlog.
* Layout: engine/library code 0x100000-0x1F0000, gameplay/UI 0x1F0000-0x30C000 (Menu, MainGame, Create, Locker, OnlineMenu overlays load at 0x4D2C00+).
* Node base class and Chara object: sections 2-3. Largest `new` sizes: 0x8D4 (530460 family, menu widgets), 0x810 (Chara).

## 8. Code change (this session): Chara guard in `overrides/svr06_overrides.cpp` (+`.bak_charaguard`)
* Hooks `0x239B40` (Chara dtor: logs `[chara] dtor this flag ra th`, remembers last 64 destroyed objects) and `0x23CC30` / `0x23AC90` (update): if the object's vtable word is outside the Chara vtable range 0x37E000-0x38A000 the update is **skipped** and logged `[chara] BAD update ... (previously destroyed | never seen destroyed)`. Disable with `SVR06_NOCHARAGUARD=1`.
* Purpose: stop the demo freeze and record who destroys the object (thread id, ra) so the teardown-vs-update ordering can be fixed at the source.

## 9. Arena allocator and why objects dangle (third pass)
* Arena table at `0x3BC030` (24-byte entries: start, end, ... per arena, up to 16), current arena index in `$gp-0x6B24` (set by `0x119B20(id)`, read at `0x119B30`'s sibling), arena count in `$gp-0x72CC`.
* `0x119BF0(first_id, sizes[])` partitions memory into arenas (called from `0x1DF7E4`, `0x20AB5C` = level/match init); `0x119B60(id, 0)` **resets an arena wholesale** (rewrites the first block header to one free block; no destructors). Allocation entry points `0x119800/0x119850` use the current arena.
* Character spawn (`0x1FB630(manager, chara, ...)`) switches arena to `manager+0xD4`, allocates helpers, registers the Chara with the manager (`manager+0x10C/0x140/0x108` fields), then restores the arena.
* Therefore a match/demo teardown frees its Chara objects by **resetting the arena**, not by running `0x239B40`. Anything still registered (update list entries pointing at `Chara+0x1A0`) then points at memory that the next arena user overwrites ("FIMC" Node / bone matrices). On the PS2 this is harmless because the manager's list is cleared first and updates stop before the reset; in the recomp an update still runs after the reset (ordering/thread issue).
* Consequence for the guard (section 8): the dtor log will usually stay silent; the `[chara] BAD update ... (never seen destroyed)` line is the expected signature. The real fix is to find the caller of the arena reset (`0x119B60` via the public wrappers near `0x1199C8`-`0x119DE4`) and the update caller, and keep their order; the guard makes the freeze impossible meanwhile.
* Remaining backlog: name the 1,327 un-anchored modules; map sound command path (sid 0x300/0x301) and movie object from code; unknowns are listed in `analysis/codeaudit/modules_roles.tsv`.

## 10. Who wipes the arenas (static trace)
* `0x119BF0(first_arena, sizes*)` re-partitions arenas (wipes them). Only two wrappers call it: `0x1DF7C0` (called from `0x1DF938`) and **`0x20AB10`** ("scene arena setup": `0x20AA70`, `0x1005C0`, `0x119340(0x4A24C0, arg, 0x70)`, then `119BF0(2 or 4, 0x4A24B8 / 0x4A24C0)` chosen by a flag at `[0x1DD200()+0xA14]`).
* `0x20AB10` is invoked by overlay code on scene entry: MainGame `0x51D888`, event `0x5A57B4/0x5B0354/0x5B0390/0x5B0424`, moves `0x5A27AC`, Locker (via `0x20AA70` at `0x5B1A2C`). So arenas 2..N are rebuilt each time a scene overlay starts, i.e. everything allocated by the previous scene (Chara objects, managers, pose buffers) dies instantly without destructors.
* Rule the engine relies on: the previous scene's update tasks must be removed from the main task list before the next overlay's `0x20AB10` runs. In the recomp a task registered with `Chara+0x1A0` survives into the next scene on some path (demo -> next scene/title).
* `0x1DF770/780/790/7A0/7B0` are small virtual getters of the heap-policy object (config table at `0x3637A0`): arena count, ids, size 0x29E, flag 1, and `0x1DF7B0` = tail call to `0x1198C0` (arena init at boot).
* Instrumentation added (no launch needed to read later): `[arena] repartition #n first sizes ra th charaBad` in `svr06_overrides.cpp` (hook on `0x119BF0`). The first `[chara] BAD update` after an `[arena] repartition` line proves the order; its `ra` names the scene-entry caller and `th` the thread.
