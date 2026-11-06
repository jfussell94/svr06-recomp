# SvR 2006 (PAL SLES-53676) recomp - project plan
Source of truth for scope: `../wwe_svr_06_recompilation_roadmap_claude_instructions.md`.
Note: the PAL disc ships CRI-based media (ADX/Sofdec), not plain IPU/SPU streams - HLE those first (see config/hle_plan.md). PSP target is out of scope until PS2 boots.

## Done (this prep pass)
- [x] ELF parsed: unencrypted, entry 0x100008, 1 code segment (2.6 MB), 9,832 function candidates, 169 syscall stubs
- [x] Middleware inventoried (CRI ADX/Sofdec/SVM, eenet)
- [x] Disc indexed: 102 DPAC, 26 AFS, 20 IRX (analysis/asset_index.json)
- [x] Config/HLE plan/setup scripts scaffolded

## Recompile result (done, verified)
- PS2Recomp built (analyzer + recompiler); analyzer found 246 SDK stubs, 7 jump tables, 54 patches.
- Code ends at 0x30C4D0; everything above is data and is in `skip` (214 entries). Result: 7,226 functions -> 7,240 C++ files in `out/generated/`, 0 errors, ~2,000 control-flow warnings (unresolved JR/JALR, auto-promoted fallback entries; see analysis/recomp_log.txt).
- Config: `config/svr06_ps2recomp.toml`. Your PS2Recomp clone is at `..\PS2Recomp` (used instead of third_party/).
- Rerun: `ps2_recomp config\svr06_ps2recomp.toml`.

## Next
1. `scripts\setup.ps1` (builds PS2Recomp). 2. Ghidra pass -> ghidra_functions.csv. 3. Run ps2recomp; fix jump tables / VU0 macro ops.
4. Runtime: 32 MB memory, syscalls, SIF RPC/CD HLE, boot to the Yuke's logo with SDL window.
5. GS->Vulkan; VU1 via VIF1 packet capture. 6. CRI HLE (audio, movies). 7. 60fps/widescreen patches. 8. Mod loader.
