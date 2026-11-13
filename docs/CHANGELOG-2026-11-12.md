# Changelog 2026-11-12

## CL-0212 Whole-game reverse-engineering pass (tools only; no game data)
- New analysis tools in `tools/`: `symbols_all.py`, `callgraph.py`, `propagate_names.py`, `global_families.py`, `role_tags.py`, `find_magic.py`, `group_paths.py`, `vtables.py`, `vtable_ctors.py`, `vtable_names.py`, `class_strings.py`, `top_unnamed.py`, `unlabelled_classes.py`, `ptr_tables.py`, `ptr_tables_ovl.py`.
- Hand-verified name inputs: `tools/names_manual.tsv`, `tools/names_manual_ovl.tsv`, `tools/class_labels_manual.tsv`.
- Formats decoded from code: POF0 relocation, Yukes chunk loader, HMD collision records, GMPD header, GM loader pipeline, BG.PAC STG seat scripts.
- Coverage at end of day: 50.9% of 23,294 functions carry a semantic label (571 hand/string/kernel-verified names, 190/190 vtable classes labelled heuristically, 1,045 function-pointer-table entries). Heuristic labels are unverified.

## CL-0213 Bug hunt (build OK, not yet run in game)
1. Guest `memcpy` used the host `memcpy` on possibly overlapping guest ranges (UB); now `memmove` (`Kernel/Stubs/LibC.cpp`).
2. GS: TEST.ZTE=0 now also suppresses the depth write (`gs/gs_cpu_backend.cpp`).
3. IOP: DMA-complete interrupt handlers are deferred while the guest holds a `CpuSuspendIntr` section, capped at 256 deferrals because interrupt state is global (`ps2xIOP/src/emulator/iop_emulator.cpp`).
Not changed: IntrMan `DisableIntr` writes the irq number to `*res` instead of the previous enable state (unverified).

## Open
Entrance: ~60 s pause between the first and second `[resload]` of the event overlay (0x5B801C -> 0x5B85E0) before the cutscene starts; cause not found yet.
