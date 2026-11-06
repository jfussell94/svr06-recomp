# Ghidra pass (needed before recompiling)
1. Install Ghidra 11.x + the Emotion Engine loader/processor (ghidra-emotionengine-reloaded).
2. Import `assets/SLES_536.76` (ELF, MIPS:LE:64:R5900). Run auto-analysis; apply PS2 SDK function-ID if available.
3. Run the PS2Recomp Ghidra export script -> `analysis/ghidra_functions.csv`.
4. Name the ~169 syscall stubs first: `analysis/functions.csv` rows named `syscall_<n>` (negative = SDK syscalls, e.g. -ve numbers via v1).
5. Label CRI middleware by version strings in `analysis/strings_interesting.txt`.
