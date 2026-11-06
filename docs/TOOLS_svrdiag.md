# svrdiag - external diagnostics (2026-10-28)

`tools\svrdiag.py` reads logs and RAM dumps only (never touches the running game). Run with `python tools\svrdiag.py <command>`.

| Command | Use | Issue it helps |
|---|---|---|
| `triage [err] [out]` | one-page report: missing-target jumps (decodes s0[0] as vtable or 4CC tag), dead main thread, frozen picture, audio clip/silence, VU1 speed, canary | demo freeze, hangs |
| `spu [err]` | SPU2 timeline: ring peak, voices, ADMA; counts silent and clipped reports, lists real KONs | distorted/stopped sound, missing SFX |
| `vif [out]` | `[vifmat]`/`[vifscan]` summary; flags out-of-range matrix floats (integer-format uploads can false-flag) | garbled geometry ISS-101 |
| `mat [err]` | `[matwatch]` packet changes per vsync with writer-thread pcs | who builds the matrix |
| `ram DUMP hex/find/refs/obj/heap` | hexdump, pattern search, pointer references, object/vtable check, heap block walk on `dumps\ram_missing_*.bin` | dangling pointers, heap bugs |
| `fn ADDR` | generated file + instruction listing around a guest address | reading the code at a pc/ra |
| `crash` | latest Windows APPCRASH events for svr06run.exe | silent crashes |

Verified on the 2026-10-28 15:00 UTC run: triage reproduces the dead-main-thread diagnosis (`s0[0]` = "FIMC", th1 pc 0x842FF), spu shows 140/150 silent reports with clip level 18304, vif/mat show the matrix packet is sane in that run.

## Added 2026-10-28 (later): live control + unattended supervision

| Tool | What it does |
|---|---|
| `tools\svrlive.py` | Talks to the RUNNING game through the existing live console (`PS2Recomp\build_svr06b_clang\analysis\live\cmd.txt`/`out.txt` - the game's cwd is the build dir; override with env `SVR06_LIVE`; polled every 15 frames by the presenter): `ee`/`w32` read RAM, `poke`, `ramdump` (-> `analysis\dumps\live_<time>.bin`, usable by `svrdiag ram`), `vramdump`, `vu1d/vu1c/vu0d`, `press`/`seq` (scripted pad input), `watch ADDR` (poll a word until it changes, 15-frame resolution), `obj ADDR` (vtable/dangling check on live memory), `frame`, `flag`, `quit`. Exit code 2 = game does not answer. |
| `tools\svrwatch.py` | Unattended supervisor for a running game: triggers on missing-target jump, dormant main thread, 5 identical screenshots, log stall 90 s, console silent 60 s, canary, audio silence after sound; then takes a live RAM dump, runs `svrdiag triage`+`spu` and writes `analysis\reports\<time>.md`. Options `--max-min N --kill --autostart --once`. |
| `svrdiag build` | Summarises `analysis\build_log.txt` (errors, FAILED steps, "no work to do" warning). |

Research notes (what similar PS2 recomp projects use, and what we still lack): PCSX2 as the behavioural oracle (RAM/register dumps for diffing), VU1 native-vs-interpreter `verify` mode (we have the A/B `SVR06_VUAB` equivalent), savestate-style checkpoints so a ~8 min load is paid once, scripted input loops, pipeline driver scripts. Not built yet: checkpoint/savestate (needs EE threads + IOP + GS/SPU2 state capture), PCSX2 oracle comparison, load-speed profiler.
