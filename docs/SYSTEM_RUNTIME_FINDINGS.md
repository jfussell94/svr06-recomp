# System / runtime findings (EE kernel, threads, memory, IOP, overlays, saves, input, tooling)

Written 2026-10-27. Addresses are for the PAL main ELF unless stated. No game data included.

## 1. Binary and boot
- ELF `SLES_536.76`: unencrypted, entry `0x100008`, one 2.6 MB code segment ending at `0x30C4D0`; everything above is data (214 TOML `skip` entries). 9,832 function candidates; 7,226 recompiled functions at the first pass, more after the hidden-start additions.
- crt0 (`0x100008`): `SetupThread(gp=0x3A0A70, stack=0x1FF0000, size=0x10000, ...)`, `SetupHeap(0x62A180, -1)`. Stack `0x1FF0000..0x2000000`; heap ends at the stack bottom on real hardware.
- Boot sequence: IOP module loads -> disc file-table scan (AFS archives) -> memory-card check -> PAC loads -> logo -> brand video -> title movie -> Press START -> idle demo or menus.
- Retail ELF has no symbols; functions were found via JAL targets, prologues, data-pointer scans.

## 2. Kernel and scheduler
- Threads are cooperative guest threads in `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` and `Kernel/Syscalls/Thread.cpp`. Status enum: 0 Running, 1 Ready, 2 Waiting, 3 WaitingSuspended, 4 Suspended, 5 Dormant.
- Syscall wrappers: table at `0x126BC0` (16 bytes apart, `li v1,N; syscall`). SleepThread `0x126F00`, WakeupThread `0x126F10`, CreateSema `0x126FE0`, SignalSema `0x127000`, WaitSema `0x127020`, StartThread wrapper `0x128120`. The Dispatcher maps numbers in `Kernel/Syscalls/Dispatcher.cpp`.
- Thread-creating calls must return the thread id on success.
- Main loop `0x14CBB0` runs optional hook pointers (`0x3C8D40 / 0x3C8D48 / 0x3B5E70`) then sleeps until vsync (`14cb88` -> `14bc40` -> syscall `0x32` SleepThread). Frame-sync threads `0x14B798 / 0x14B8A0 / 0x14B990` (th7/th8/th9) are woken from INTC callbacks (`0x14B640 / 0x14B460 / 0x14B4E8` callback table, `iWakeupThread`). Game INTC handlers: VBLANK_S `0x11A6D0`, timer2 `0x131F50`, DMAC ch1 (VIF1) `0x1B1BB0`.
- Game logic mostly runs from the vsync callback `mainCb = 0x1BFC70` plus job threads: T3 entry `0x1C0980` (sema 6), T5 `0x113C80` (sema 7), T2 `0x2D7490` (sema 3, SIF RPC thread), th1 main (crt0 entry), th4 (`0x1C0DB0`). Thread 6 (`0x14B6A8`) is suspended at entry by design (resumed only inside a priority-1 critical section).
- `[watchdog] STUCK? running=0` is a normal per-frame state; real stalls show Dormant threads or repeated missing-target lines.
- Object model: vtable at `+0`; secondary-base thunks `addiu a0,a0,-0x1a0; j target`. Sample update method `0x23AC90` (at `0x23AD8C`: `lw t9,0(s0); lw t9,0x80(t9); jalr`), thunk `0x23CC30`, vector update loop `0x1067B8..0x1067C0` (vtable +0xc), resource load fn `0x21CB20`.

## 3. Memory
- 32 MB EE RAM (`PS2_RAM_SIZE`), user wants memory limits raised only if needed for performance/mods (extra headroom as an optional "large memory mode", off by default, since 1:1 is the base).
- Guest heap: see ISS-009. Arena table `0x3AC030` (24 B/entry: base, end, ...); current arena index `[gp-0x6B24]` (`0x399F4C`), count `[gp-0x72CC]`. Arenas 0..14 carved from the top (`0x1EFBFF0` downward, 8.4 MB, six are 1 KB placeholders); arena 15 = main (`0x62A180-0x16F37F0`, 17 MB). Block header 16 B: `{next_idx | flags<<28, prev_idx, nextFree_idx, arena}` (indices in 16-byte units from the arena base); free list head at `[base+8]`.
- Allocator entry points: `new` `0x119800/0x119850` -> core `0x119E70/0x119FA0`; `free` `0x11A410` (loop at `0x11A47C`); heap init `0x11ABF0` (`malloc` libc `0x2F5FC0`, shrinking by `0x4000` until it succeeds) -> `heapInit` `0x1198C0` via wrapper `0x1DF7B0`. File-buffer allocator `0x1E1440` (memalign 0x40; size from `0x101290`, rounded to 0x800).
- Large loads: 14 MB `GAME` motion block (`m.pac`, MOT group, entry GAME, `0xD5F900` bytes; memalign `0xD60000` -> `007B7240`), PAC resource load (archObj `016B3920`, hdr `016B3940`). Corruption after these: ISS-100.
- Quadword access: `LQ/SQ` need 16-byte alignment; the recompiler masks addresses. Fast path `Ps2FastRead*` masks to RAM, slow path goes through the runtime.

## 4. IOP
- `ps2xIOP`: module manager, RPC servers, iop_cdvd (`readSectors`, logs `[iopcd]`, EE side `[cdrd]`). 20 IRX under `MODULE\`. DNAS (`dnas.bin`) and online code are not supported.
- SIF RPC SIDs: see `findings/audio-findings.md`. `loadModuleBuffer` pointers are IOP addresses; `sceCdSearchFile` must be HLE'd.

## 5. Overlays
- Full description in `findings/overlays.md`. Summary: 11 overlays in `BIN\BIN.PAC`, two slots (lower `0x4D2C00`, upper `0x5A2700`), loaded verbatim, MWo3 header. Registered via `svr06ovl::addBank/addFunc/addLabels/find`. `find()` resumes mid-function addresses in the nearest preceding generated function through its `ctx->pc` switch, so every mid-function entry needs a `case` label.
- Generated overlay files must live in `svr06-recomp/overrides/` (copied into the runner on every build).

## 6. Memory card
- HLE `sceMc*` in `Kernel/Stubs/MemoryCard.cpp`; host save directories `mc0\BESLES-53676SD7\` (`BESLES-53676SD7`, `icon.sys`, `SDvsRAW.ico`) and `mc1\`. Boot-time calls (`sceMcGetInfo/Sync/GetDir/GetEntSpace/Open/Read/Close`) work and the autoload screen text is correct.
- NOT yet verified: create/save/load round trip, free-space reporting, icon rendering. `tools/mcextract.py` inspects saves.
- Planned: optional PCSX2 memory-card file format (launcher asks which save format; player picks a card file; never reliant on PCSX2; backup before every write) so saves can move between this port and PCSX2.
- User order of work: memory card first, then Options -> Unlock All.

## 7. Input
- `Kernel/Stubs/Pad.cpp`, `ps2_pad.cpp`: PS2 pad emulation with keyboard (Enter/Space for Start) and XInput handling in progress. Wanted: full XInput gamepad support, remapping UI, quick resume. The shipped game gets no injected input (live console `press` is testing-only).

## 8. Tooling and workflow
- Windows project dir `C:\Users\ajizz\Documents\Recomp` (older notes say `Desktop\Claude`): `svr06-recomp\` (our project), `PS2Recomp\` (user's clone of https://github.com/ran-j/PS2Recomp with local changes), `assets\` (disc contents, never upload), `Tool program\` (the user's Mod Maker Tools), `tests`, `tools`, `src`.
- Build: `scripts\FAST_REBUILD_AND_RUN.cmd` -> `fast_rebuild.ps1` (kills stray builds and the game first, copies overrides, configures, builds `ps2EntryRunner` in `PS2Recomp\build_svr06b_clang` with clang-cl + Ninja, then runs `run.cmd`). Logs `analysis\build_log.txt`, `analysis\run_log.txt`; runtime stderr/stdout also in `analysis\run_err.txt` / `run_out.txt`. Runtime-only changes take about 10 minutes to link; adding 7k overlay files took about 15 minutes.
- Run: `scripts\run.cmd` kills old `svr06run.exe` / `ps2EntryRunner.exe` / PCSX2, finds `SLES_536.76` under `assets\` (or `SVR06_ELF`), sets env vars (`SVR06_GSDUMPTICK/LEN`, `SVR06_TRACE`, `SVR06_TURBO=1`, `SVR06_SKIPMOVIES=0`, `SVR06_DUMPDIR`, `SVR06_MAXSECS`) - these are debug leftovers to review before release (no timers or run-time limits in shipped builds).
- Live console (`SvrConsole.cpp`): reads `PS2Recomp\build_svr06b_clang\analysis\live\cmd.txt` (relative to the game's working dir; the copy under `svr06-recomp\analysis\live` is NOT read), writes `out.txt` there. Commands: `ee ADDR LEN`, `ee32`, `poke32 ADDR HEX`, `vram`, `ramdump`, `vramdump`, `press <btn> <frames>` (testing only), `seq`, `flag`, `frame`, `quit`; `touch live/threads.req` -> `live/threads.txt`.
- Device VM: Linux, `$HOME=/sessions/<id>`, project mounted at `$HOME/mnt/Recomp`; cannot run Windows builds or the game; commands time out at 120 s (180 s max); background processes die when the tool call ends; file deletion needs `device_request_delete_permission`. The Linux `ps2_recomp` used for overlays lives in `~/prc` (needs `pip install --user cmake ninja` and a GCC 11 include fix in `code_generator.h`). Overlay images decode into `~/ovl/pac/` (never stored in the project).
- Computer use (screenshots/clicks) needs `computer_request_access`; terminals/Explorer are click-only tier; the user may use the PC while work runs.
- Reference emulator: PCSX2 (close old instances before starting a new one). Reference docs: ps2tek (https://psi-rockin.github.io/ps2tek/), project doc `reference/ps2tek.md`. Another PS2 recomp for comparison: `hctp-recomp-main.zip`.
- Disassembly helpers (VM): `~/tl/dis3.py` with `RAM=$HOME/ram8.bin` (main ELF), `~/vu/vudis.py` (VU1). `grep -h "^ *// 0x" sub_XXXXXXXX_0xXXXXXXXX.cpp` prints the MIPS comments of a generated function.

## 9. Licensing / publication notes
- The IPU/MPEG decode path is derived from PCSX2 (GPLv3): attribution required.
- The repo must publish without any game data: run `python tools\check_clean.py` first; never upload `assets\`, `analysis\dumps\`, `analysis\live\*.bin/*.png/*.ppm`, `analysis\shots\`, `out\`, generated `sub_*.cpp` / `ovx*` / `ovlD_*` files, or decoded overlay images.
