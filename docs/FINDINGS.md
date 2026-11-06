# SvR 2006 (PAL, SLES-53676) PS2 recompilation - running findings
(Working notes. The final write-up is produced when the game is fully playable. Contains no game assets.)

## Engine / kernel structure
- Entry 0x100008. Retail ELF has no symbols; functions found via JAL targets, prologues, data-pointer scans.
- EE syscall wrappers: table at 0x126BC0, 16 bytes apart (`li v1,N; syscall`). SleepThread 0x126F00, WakeupThread 0x126F10, CreateSema 0x126FE0, SignalSema 0x127000, WaitSema 0x127020, StartThread wrapper 0x128120.
- Main loop is 0x14CBB0: runs optional hook pointers (0x3C8D40/0x3C8D48/0x3B5E70), then sleeps until vsync. Frame-sync threads 0x14B798/0x14B8A0/0x14B990 are woken by an INTC handler (iWakeupThread).
- Game registers: INTC cause 2 (VBLANK_S) handler 0x11A6D0, INTC cause 11 (timer 2) handler 0x131F50, DMAC ch1 (VIF1) handler 0x1B1BB0.
- Thread 0x14B6A8 (prio 8) is created and then suspended by the game; something must resume it.
- CRI middleware (ADX/Sofdec) runs on the IOP via SIF RPC (SIDs 0x300/0x301, async callbacks signal semaphores).
- Boot sequence: IOP module loads -> disc file-table scan (AFS archives) -> memory card check -> PAC loads.

## Runtime pitfalls found (PS2Recomp)
- loadModuleBuffer pointers are IOP-side addresses.
- sceCdSearchFile must be HLE'd; fileio/Fs RPCs stubbed.
- Thread status enum: 0 Running,1 Ready,2 Waiting,3 WaitingSuspended,4 Suspended,5 Dormant.
- Direct tail calls bypass the function table; hooks catch jal/jalr calls.

## Tooling
- overrides/svr06_overrides.cpp: watchdog, call tracer (SVR06_TRACE env), first-call coverage (SVR06_NOCOV=1 disables).
