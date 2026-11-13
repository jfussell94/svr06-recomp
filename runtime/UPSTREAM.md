# Upstream runtime

This project builds on https://github.com/ran-j/PS2Recomp (MIT, see its LICENSE).
Pinned commit: `eb8688b2fd191129f103a39543002c005b33409c` ("Emulate PS2 float semantics for EE FPU results and DIV.S/RSQRT.S (#272)", 2026-10-14).

`runtime/patches/0001-svr06-runtime.patch` contains all local changes to PS2Recomp (about 45 files):
IOP/CD/RPC fixes, EE scheduler and syscall behaviour, software GS/VU1 work, memory card HLE,
the native movie player (`SvrMovie.cpp`, FFmpeg), ADX music (`SvrMusic.cpp`), and the live debug console (`SvrConsole.cpp`).
It contains no game data. Apply it with `scripts\setup.ps1`, or manually:

    git clone --recursive https://github.com/ran-j/PS2Recomp
    cd PS2Recomp && git checkout eb8688b2fd191129f103a39543002c005b33409c
    git apply --ignore-whitespace ../svr06-recomp/runtime/patches/0001-svr06-runtime.patch

The patch includes some debug code that writes dumps to `analysis\` (relative to the working directory) and a thread/semaphore syscall logger (`[sys]` lines, first 12,000 events). Both are marked for cleanup in `docs\ROADMAP.md`.
