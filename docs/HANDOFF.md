# HANDOFF — WWE SmackDown! vs. Raw 2006 (PAL, SLES-53676) native recompilation

Written 2026-10-25 for an agent taking over. Contains no game data. Read this top to bottom once, then use sections 6–9 as a working reference.
Everything here comes from the project files, the earlier sessions and the user's own words. Items marked **(unverified)** are my best recollection or inference and should be checked before you rely on them.

---

## 1. The goal and the person

- **Goal:** a fully playable native PC build of WWE SmackDown! vs. Raw 2006 (PAL PS2, serial SLES-53676, ELF `SLES_536.76`), produced by static recompilation with the PS2Recomp toolchain (`ps2_recomp` recompiler + `ps2xRuntime` runtime + `ps2xIOP` IOP emulation). The game is a Yuke's engine title using CRI middleware (ADX audio, Sofdec video).
- **User:** "ajizz", Windows PC. Project root `C:\Users\ajizz\Desktop\Claude`. Contact address on the account: la.jfussell@gmail.com (identification only).
- **Autonomy:** the user's standing instruction is "take over, do what you need to, including running auto_loop.cmd if you change anything. You can also use PCSX2. You are on your own and I expect everything to be working later today or you are fired." Later: "do the rest without me", "just come back to that for later, sort issues by critical to minor and do them in critical status first", "the actual game doesn't need input".
- **Hard constraints from the user:**
  1. **Never upload game data** (`live_ram.bin`, `live_vram.bin`, ISO/AFS/PAC contents, ELF, generated game code). The project repo is meant to be publishable with no game assets (`python tools\check_clean.py` before publishing).
  2. **No injected or scripted input in the shipped build.** Fix real causes. Real input only from the user (or from the live console for *testing*, never baked into the build). The title idle timer and demo must behave like the console.
  3. **No run-time limits/timers** in builds.
  4. **Report a completion percentage** after each run/result.
  5. When done, produce a **findings document** (`docs\FINDINGS.md`) with all research, for future projects and people curious how these games work.
  6. Always close old PCSX2 instances before starting a new one.
  7. Prefer debugging the **running game directly** (live hooks/console) over waiting on logs.
  8. Wants: gamepad (XInput) support, native 60 fps, widescreen/ultrawide, 4:3 with proper scaling (not stretching), all regions eventually (USA/Japan/Korea/Italy folders under `assets\`).
- **The user sometimes uses the PC while you work.** Terminals/Explorer are click-only for computer use. Windows-side commands only run through the user's `auto_loop.ps1` window (section 4).

---

## 2. Machines, folders, and how you reach them

| What | Where |
|---|---|
| Project (ours) | `C:\Users\ajizz\Desktop\Claude\svr06-recomp\` (device shell: `~/mnt/Claude/svr06-recomp`) |
| Runtime + recompiler (user's clone of ran-j/PS2Recomp, with our local modifications) | `C:\Users\ajizz\Desktop\Claude\PS2Recomp\` |
| Disc contents (extracted folder, not an ISO file) | `C:\Users\ajizz\Desktop\Claude\assets\` |
| Original roadmap/instructions | `C:\Users\ajizz\Desktop\Claude\wwe_svr_06_recompilation_roadmap_claude_instructions.md` |
| PCSX2 reference snapshots of real game flow | `C:\Users\ajizz\Documents\PCSX2\snaps` |
| User's media tool (reference only, do not copy game data) | `C:\Users\ajizz\Desktop\New folder\SVR\Program` (`afs_reader.py`, `Mod Maker Tools/ps2_media.py`, `discfs.py`, `ps2_char.py`, `jboy.py`) |
| Memory card host dir | `...\@WWE SMACKDOWN! VS. RAW 2006 (EUROPE, AUSTRALIA).ISO\mc0\BESLES-53676SD7\` (contains `BESLES-53676SD7`, `icon.sys`, `SDvsRAW.ico`) |
| Ghidra 12.1.4 + ghidra-emotionengine-reloaded | `svr06-recomp\third_party\ghidra`, `...\ghidra-emotionengine-reloaded` |
| Also lying around | `hctp-recomp-main.zip` (a different PS2 recomp for another game, useful as a comparison), `WWE SmackDown! vs. Raw 2006 (USA).pdf` (manual), `wwe-smackdown-vs-raw-2006.9929.max` |

**Two execution environments, do not confuse them:**
- Your cloud container (`Bash`, `Read`, `Write`) has no Windows toolchain. Files on the user's PC are reached through the device bridge (`mcp__remote-devices__*`).
- `device_bash` runs in a **Linux VM** on the user's PC with the Claude folder mounted at `~/mnt/Claude`. It can read/write project files and run python, but **cannot run cmake, the Windows build, or the game**. Windows commands run only through the user's `auto_loop.ps1` window.
- File deletion in the device shell is "Operation not permitted" (append-only for `live/cmd.txt`, `threads.txt`). Use `device_request_delete_permission` if you ever need to delete.
- Device shell commands time out at 120 s. A recursive `grep -r` over `PS2Recomp\ps2xRuntime\src\runner` (thousands of generated files) will time out; grep specific files, or use `grep -l` with `timeout`.
- To view an image from the device: copy it into the mounted project folder, `device_stage_files` it, then `Read` the staged path under `/mnt/user-data/uploads/Claude/...`.

---

## 3. Project history (what was done before the most recent sessions)

Chronological, from `docs/ROADMAP.md`, `docs/FINDINGS.md`, `PS2_PROJECT_STATE.md` and the sessions.

1. **Prep pass.** ELF parsed: unencrypted, entry `0x100008`, one code segment (2.6 MB), 9,832 function candidates, 169 syscall stubs. Middleware inventoried: CRI ADX/Sofdec/SVM, eenet. Disc indexed: 102 DPAC, 26 AFS, 20 IRX (`analysis/asset_index.json`). Config/HLE plan/setup scripts scaffolded.
2. **Recompile.** PS2Recomp built (analyzer + recompiler). Analyzer found 246 SDK stubs, 7 jump tables, 54 patches. Code ends at `0x30C4D0`; everything above is data and is in the TOML `skip` list (214 entries). 7,226 functions → 7,240 C++ files in `out/generated/` (copied into `ps2xRuntime\src\runner\`), 0 errors, ~2,000 control-flow warnings (unresolved JR/JALR, auto-promoted fallback entries). Config: `config/svr06_ps2recomp.toml`. Rerun: `ps2_recomp config\svr06_ps2recomp.toml`.
3. **Runtime bring-up.** 32 MB memory, syscalls, SIF RPC/CD HLE, thread/semaphore scheduler (`EeScheduler.cpp`), boot to the Yuke's logo, GS software rasterizer, VU1 interpreter, native audio hooks. Pitfalls found: `loadModuleBuffer` pointers are IOP-side addresses; `sceCdSearchFile` must be HLE'd; fileio/Fs RPCs stubbed; direct tail calls bypass the function table (hooks catch jal/jalr only).
4. **Fade/clear quad investigation** (resolved red herring): zero-size quad `0x644EB0` is layout item '001' with (w,h)=(0,0) by data; degenerate quads are expected, not the garble cause.
5. **libgs insight:** PCSX2's per-frame header is a libgs `sceGsDBuffDc` packet (22 A+D writes: FRAME fbp 0/0x50 alternating, XYOFFSET field offset, SCISSOR 640x256, clear sprite). Our GS stream lacked it because libgs is HLE-stubbed via TOML stubs (`sceGs*@0x11xxxx`) although recompiled originals exist (`sub_0011D840` etc.). Plan was to restore originals at runtime (replaceFunction).
6. **Movie system.** Found the "E211151: ADXF_GetFnameRangeEx() faild." blocker (ADXF partition table at `0x3129d0` all zero). Resolved by a **native movie player** (see section 6) rather than fixing IOP-side ADXF.
7. **Overlay system.** The game loads code overlays at runtime into an arena `0x4D2C00..0x5A2700`. We recompiled the overlays separately (banks A, D, L, M, S etc.; packs `analysis/ovl*_pack*.tgz`, generated `overrides/ovlD_*.cpp` ×1168, `overrides/ovl_generated/`, `svr06_ovl_demo.cpp`) and register them via `svr06ovl::addBank/addFunc/find` (signature words verified against RAM before use).
8. **PCSX2 reference.** Menu walk captured with PCSX2 (`analysis/pcsx2_menu_walk_*`, `pcsx2_menu_inputs.p2m2*`, `analysis/ref/`) for comparing real behaviour.
9. **Last recorded status (from `PS2_PROJECT_STATE.md`, now stale): ~96% title flow / ~58% playable.**

---

## 4. Build and run workflow (critical, easy to get wrong)

### The auto loop
`scripts\auto_loop.ps1` (launched via `scripts\AUTO_LOOP.cmd`, runs in the **user's** PowerShell window; sometimes needs admin) does this:
1. Watches `svr06-recomp\overrides\` and `analysis\go.flag`.
2. Copies `overrides\*.cpp` → `PS2Recomp\ps2xRuntime\src\runner\`.
3. Builds target `ps2EntryRunner` in `PS2Recomp\build_svr06b` (log: `analysis\build_log2.txt`, ~70 MB; the end shows `==== BUILD STEP FINISHED ====`).
4. Runs `scripts\run.cmd` (game log: `analysis\run_log.txt`).
5. Writes `analysis\auto_done.txt` when a cycle ends.

**Facts that cost hours to learn:**
- A change is applied only by: edit `overrides\*.cpp` (or runtime sources) → `touch analysis/go.flag` → **`echo quit >> analysis/live/cmd.txt`** to end the running game. The loop then rebuilds and relaunches. `touch go.flag` alone does not restart a running game.
- **After a plain `quit` with no source change the loop may NOT relaunch.** Appending text to `analysis/go.flag` (`echo relaunch >> go.flag`) relaunches it. (Confirmed 2026-10-24 15:03–15:08.)
- Link takes **10–15 minutes** (large override files plus ~1,168 overlay objects). Plan work in batches; do not rebuild for one-line probes if the live console can do it.
- If the user closes the loop window, nothing rebuilds or relaunches. Ask them to reopen it ("its up" is how they confirm).
- Game freezes ~10–20 s after launch have occurred when the machine or loop state was bad; reopening the loop fixed it.
- Never clean-build. Never edit generated `sub_*.cpp` or `out_*.cpp`. Fixes go in the runtime layer, the TOML, or `overrides/svr06_overrides.cpp` / `svr06_overlay.cpp`.
- Circuit breaker (from `PS2_PROJECT_STATE.md`): after 3 identical failures stop, consult PCSX2/docs, change approach.

### The live console (the best debugging tool)
Append commands to `svr06-recomp\analysis\live\cmd.txt`; output lands in `analysis\live\out.txt`:
- `ee ADDR LEN` / `ee32` read EE memory; `poke32 ADDR HEXVAL` write.
- `vram` / `ramdump` / `vramdump` write `live_ram.bin` / `live_vram.bin` (**never upload these**).
- `press <btn> <frames>` (for testing only), `seq`, `flag`, `frame`, `quit`.
- `touch live/threads.req` → `live/threads.txt` (thread/semaphore dump).
- The run log also gets a `[watchdog]` block every ~20 s when stuck: thread status (0 Running, 1 Ready, 2 Waiting, 3 WaitingSuspended, 4 Suspended, 5 Dormant), wait reason, pc/ra/entry, semaphore counts, `vsyncTick`.

### Viewing the screen
- The game's own screenshot capture (`analysis/shots`) returns black/6,716-byte images (**broken**, low priority to fix).
- Use `vramdump` + the decoder `/tmp/dec.py base fbw out.png` (PSMCT32 swizzle decode; frame buffers at base 0 and 140, fbw 10). `dec.py` lives only in the device VM's `/tmp` and may be gone; it is a small PSMCT32 de-swizzle script. The decode is approximate (images look garbled but menus are recognisable).
- `computer_screenshot` needs `computer_request_access` first (user approves). Browsers/terminals are read-only/click-only tiers.

---

## 5. Runtime architecture you must understand

- **Scheduler:** the game's own threads are cooperative guest threads in `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` and `Kernel/Syscalls/Thread.cpp`. Syscall wrappers: table at `0x126BC0` (16 bytes apart, `li v1,N; syscall`). SleepThread `0x126F00`, WakeupThread `0x126F10`, CreateSema `0x126FE0`, SignalSema `0x127000`, WaitSema `0x127020`, StartThread wrapper `0x128120`. `ResumeThread/WakeupThread/StartThread/…` must return the **thread id on success**, not 0 (the game does `if (ResumeThread(tid)==tid) WakeupThread(tid)` every frame).
- **Main loop** `0x14CBB0` runs optional hook pointers (`0x3C8D40/0x3C8D48/0x3B5E70`) and sleeps until vsync. Frame-sync threads `0x14B798 / 0x14B8A0 / 0x14B990` are woken by an INTC handler (iWakeupThread). Game INTC handlers: VBLANK_S `0x11A6D0`, timer2 `0x131F50`, DMAC ch1 (VIF1) `0x1B1BB0`.
- **Game logic mostly runs from the vsync callback** `mainCb = 0x1BFC70` plus job threads: T3 entry `0x1C0980` (sema 6), T5 `0x113C80` (sema 7), T2 `0x2D7490` (sema 3, SIF RPC thread). Main thread T1 sleeps at `0x126F08`.
- **Thread `0x14B6A8` (prio 8, "T6")** is created and then *suspended by the game*; something must resume it. It busy-waits on flag `0x3161E8` while incrementing a counter at `0x316190`. In every stalled run it is `Suspended` at its entry point (pc==entry, ra=0), i.e. it never ran. **This is the strongest lead for the intro stall (section 7).**
- **IOP:** emulated in `ps2xIOP` (diagnostics via `iopdbg`). SIF RPC SIDs: `0x80000900` = sound slot status polling (`0x1A3F68`), `0x90000200` = MPEG RPCs, `0x300/0x301` = CRI ADX/Sofdec. CRI middleware runs on the IOP; async callbacks signal semaphores.
- **Overlays:** `svr06ovl::find(addr)` also resumes mid-function by scanning back ≤0x10000 for the nearest preceding table entry (generated functions dispatch on `ctx->pc`). This fixed the demo-load freeze at `0x4EBC54` (jump into the middle of `sub_004EAF50`). Resumable functions: mid-function entries alias to their owner with `ctx->pc`.
- **Hooks:** `covThunk` in `overrides/svr06_overrides.cpp` wraps functions by address (probe lists in the `pl[]` array). Direct `jal` calls from generated code can bypass thunks; hooks catch table lookups.
- **Performance:** software GS rasterizer and VU1 interpreter run **synchronously on the main thread**. This is the main performance limit.
- **Memory card:** HLE `sceMc*` in `Kernel/Stubs/MemoryCard.cpp`. Boot sequence (`sceMcGetInfo/Sync/GetDir/GetEntSpace/Open/Read/Close`) executes fine; create/save/load have **not yet been tested** because the menus that exercise them were not reached.

---

## 6. The movie system (heavily worked, mostly done)

**Design:** HLE movie player in `ps2xRuntime/src/lib/Kernel/Stubs/SvrMovie.cpp`: FFmpeg decodes the game's `.sfd` files (MPEG-2 program streams, `mpeg1video` + `adpcm_adx`) from AFS entries under `MOVIEP/` and draws them in the raylib loop (`svr06_movie_present`). API: `svr06_movie_start/stop/done/hold/skip_requested`.

**Movie map (verified from AFS headers):** `LOGOP.AFS` = [0 sd7logos_pal.sfd 9.3 MB, 1–2 dummy, 3 sd7title.sfd 19 MB, 4 missing]; `OPP.AFS` = [0 dummy, 1 sd7_opn_smack_PAL.sfd, 2 sd7_opn_raw_PAL.sfd]; `ENDP.AFS` = [sd7ending_PAL.sfd]; `NMVP000–004.AFS` = nmvNNN.sfd titantrons (86 KB entries are placeholders). The game indexes them by "partition id": ptid 10–14 = NMVP000–004, 15 = LOGOP, 16 = OPP, 17 = ENDP. Names table in code: {"NMVP000".."NMVP004","LOGOP","OPP","ENDP"}. Intro order: ptid 15 idx 0 (logo) → ptid 16 idx 2 (OPP/brand video) → ptid 15 idx 3 (title movie, must hold last frame until Start).

**Hooks (in `overrides/svr06_overrides.cpp`):**
- `movieNameHook` @ `0x1498B8` captures the request (ptid=a0, idx=a1). Log line: `[movie] request ptid=15 idx=0`.
- `movieSkipHook` @ `0x19BCC0` (per-frame movie object update) starts playback when the object is in state 3 and sets `PLAYEND` on natural finish or on the user's skip press (Enter/Space/pad Start). Title movie holds its last frame (`svr06_movie_hold`) until Start.
- `adxGetTimeHook` @ `0x18A188`: gives the game's movie-position clock real elapsed time (written to `*a1=ms, *a2=1`), otherwise the position never advances and the natural end-of-movie test never passes. Not applied to the title movie (`s_isTitle ? 0 : now`), because it must wait for Start.
- ADX audio hooks: StartAfs `0x156D18`, GetStat `0x1544B8`, Stop `0x1543C0`, SetOutVol `0x1551D8`; the [music] log tag.

**Movie object** `F3A8C0`: `+0x48/+0x4C` state (1 prepare, 3 playing, 4 end, 6 stopped/ending); per-movie attrs at `obj+0xA04+idx*4` via `19B4F8(obj,idx)`; counters at `obj+0xD28+…`. End detector `198CA8` → `198D18/198D88/198E50/198EA8`; `198EA8` uses `19C0E0` (position num/den from `[obj+0xD28+0x28C/0x290]` via time provider `18A188`) and `19C618`=`172658`, compare `num*1000 >= attr36*den` (attr36 = 0x22291E0 µs ≈ 35.9 s). `[mvobj]` and `[mend]` probes print state transitions.

**Status:** movies play; natural end works; Start skips; the title waits for Start. In the latest run (2026-10-24 15:08 relaunch) the **whole intro ran through automatically and Start reached the main menu**. See section 7 for what is still broken.

**Media facts:** music = `BGM/BGM<k>.AFS` CRI ADX (`bgm<i>.adx`); CHAR/PRO records in `PAC/M.PAC` are 432 B (movie `+0x13C`, music `+0x13D`); AFS format: `AFS\0`, count, (offset,size)*, optional name table (ptr,len) with 48-byte name records. The user said SvR games use Bink; **checked: this PAL disc uses Sofdec MPEG-2, not Bink** (Bink is the Xbox 360 games).

---

## 7. Known problems, ordered by severity (the user's ordering: critical first)

### CRITICAL
1. **Playable match.** Not reached. Entering Play / Match Rules freezes: main thread spins, job threads T3/T5 wait forever on semaphores 6/7 (count 0, 1 waiter) with no signal. Same family of bug as item 2. Until this works the game is not "playable".
2. **Nondeterministic scene/sequencer stall.** In some launches (seen 14:03 and 14:50 on 2026-10-24) after the logo movie ends the movie object goes 4→6→1 and the game **never issues the next `[movie] request`**; Start/Cross/Circle presses do nothing. In other launches (14:30, 15:08) everything proceeds. In a stalled run: T2 waits sema 3, T3 sema 6, T5 sema 7, T4/T7/T8/T9 sleeping, **T6 (`0x14B6A8`) Suspended at entry**, vsyncTick and main `loops` still advance (so the frame callback runs), and probes on `0x182E60/0x182DD8/0x182EE0/0x1833E8/0x183B18` (the "play movie" API: `0x182E60` → `0x1833E8` → `0x183458` → `0x1498B8`) never fire, so the scene logic that should call them isn't running. Strongest hypotheses, in order:
   - T6 should be resumed (by `ResumeThread` from game code or another thread) and is not; compare ordering of `StartThread/SuspendThread/ResumeThread` between a good and a bad run. **Add logging of Create/Start/Suspend/Resume/Wakeup/Signal syscalls with thread id and caller `ra`, then diff a good run against a bad one.** Prior sessions had no such log (`grep -i suspend run_log.txt` is empty).
   - A lost or mis-ordered `iWakeupThread` / `iSignalSema` from interrupt context (INTC handler → frame-sync threads), i.e. a race between the host vsync timer and the scheduler.
   - A real timing race in the overlay/asset load that completes before/after something is registered.
   Workaround today: quit and relaunch (touch `go.flag` if the loop doesn't restart it).
3. **Memory card (user's top queued request): "memory card first".** Needs a verified create / save / load round-trip through the HLE `sceMc*` layer. Host dir and boot sequence are in section 5. Test through the game's own Options → memory card screens once reachable; check `mc0` folder contents, icon/`icon.sys`, free-space reporting (`sceMcGetEntSpace`), file format expected by the game (`tools/mcextract.py` exists to inspect saves).

### MAJOR
4. **Audio:** ADX music/SFX not fully working; "doubled music" reported after pressing Start on the first Press-START screen (see 6 below). SPU2 HLE/native ADX decode path (`[music]` hooks) is partial.
5. **Performance:** software GS + synchronous VU1 on the main thread. Plan: move GS rasterization and VU1 onto worker threads (async) or move to a GPU path (the earlier roadmap said GS→Vulkan, VU1 via VIF1 packet capture).
6. **Menu font and pink logo CLUT:** font table/atlas wrong (menu text garbled in decodes), logo uses wrong palette (CLUT). Texture upload/CLUT mode handling in the GS code needs checking against PCSX2.
7. **"NOW LOADING" screen shows a garbage texture grid** under the HBK splash (user screenshot). The demo-load freeze itself is fixed, but the visible garbage suggests wrong texture/CLUT data or uninitialised VRAM region.

### MODERATE
8. **Doubled Press-START screen:** the first one is missing textures, the second is right; pressing Start on the first doubles the music. Probably the title scene being initialised twice (two scene instances or a re-entered state), or a textures-not-uploaded-yet draw. Not investigated.
9. **Movie hitch:** the last video before the Raw/SmackDown brand video hitches on a black screen; pressing Start skips past it "but shouldn't happen" (an input leaking or a timing/prepare problem; the `stuck-prepare … -> PLAYEND` log seen in the latest run is a related symptom). Not investigated.
10. **Idle → demo → single Press START behaviour,** needs verification that demo mode starts by idle timeout without injected input.
11. **Options menu "Unlock All"** toggle (user request, queued second): add a menu entry in Options that enables unlock-all. Approach: find the Options menu overlay/layout, add a toggle writing the game's unlock flags (or patch the save/profile read path); design must not break the original menus. Not started.
12. Unhandled instructions (e.g. `DMULT`/MMI paths) occasionally logged; make sure all are handled.

### MINOR
13. Native 60 fps (the game runs 50 Hz PAL logic; needs a decoupled render rate or frame interpolation), widescreen/ultrawide and 4:3 with correct scaling, gamepad (XInput) support polish.
14. Remove debug instrumentation: `[prof]/[tprof]`, `svr06_prof.h` scopes, `[mend]`, `[hot]/[bigint]/[muldf-zero]/[calloc-fail]/[sched]/[sema]` logging, extra probe entries in `pl[]`, GS/VU1 debug (all in `EeScheduler.cpp`, `svr06_overrides.cpp`, GS/VU files). The run log reaches 5–6 MB per minute, so this also speeds up the game.
15. Fix the game's own screenshot capture (returns black frames).
16. Update `docs\FINDINGS.md` (currently 1.6 KB, mostly early notes) into the final write-up; update `PS2_PROJECT_STATE.md` (stale percentages).

---

## 8. Recent session log (2026-10-23/24), what changed in code

- `overrides/svr06_overlay.cpp`: `find()` mid-function resume (scan back ≤0x10000 for the nearest preceding entry) → fixed the demo-load missing-target at `0x4EBC54`.
- `overrides/svr06_overrides.cpp`:
  - `g_movieClockStartNs` atomic; started in `movieSkipHook` when a movie starts, set to 0 for the title movie.
  - `adxGetTimeHook` on `0x18A188` (message `[music] GetTime hook installed`).
  - `[mend]` probes for `0x19C0E0 / 0x19C618 / 0x198E50` inside `covThunk`; `pl[]` extended with `0x19C0E0, 0x19C618, 0x198E50, 0x182E60, 0x182DD8, 0x182EE0, 0x1833E8, 0x183B18`.
- Wrong hypotheses already disproved (do not repeat): poking INT_MAX into `F3B880/F3B888`; hooking `ADXT_GetTime 0x154800` (gave num=0 due to `19DEC0` smoothing); `198EA8`'s `a1` is `obj+0x950` (a delay-slot confusion earlier thought it was 0xFF000000); the zero-size quad theory for the fade garble.
- First verification of the demo-load fix was misread because the old binary was still running. Always confirm the new build is the one running (check the build log's `BUILD STEP FINISHED` time vs the run log start).

---

## 9. Roadmap for the next agent (suggested order)

1. **Confirm the baseline.** Check `auto_done.txt`/`run_log.txt` timestamps, make sure the game is up; relaunch via `go.flag` if not. Take the latest good build as the reference and record the thread-start order from the first seconds of a good run.
2. **Instrument thread syscalls** (one build): log every `CreateThread/StartThread/SuspendThread/ResumeThread/WakeupThread/iWakeupThread/SleepThread/SignalSema/iSignalSema/WaitSema/CreateSema` with tid, args, `ra`, and EE cycle, only for the first ~20 s. Capture one good and one bad run. Diff. This should explain item 2 and likely item 1 (match/Play freeze), because both look like missing wakeups. Fix in `EeScheduler.cpp`/`Thread.cpp` rather than adding game hacks.
3. **Reach a match** (critical): with scheduler fixed, drive the real menus with the gamepad or live-console presses (testing only) to Play → Exhibition → match. Expect new missing-target/unhandled-instruction issues in overlays (`svr06_ovl_*` banks may need more banks recompiled: see `analysis/recomp_overlay.cmd`, `scripts/recomp_hook.ps1`, `analysis/ovlD/*`).
4. **Memory card round-trip** (user priority #1): exercise create/save/load in the game's menus, then verify the host `mc0` directory and `tools/mcextract.py`.
5. **Options → Unlock All** toggle.
6. **Audio** (BGM ADX + SFX, no double playback), then **performance** (async GS/VU1), then **font/CLUT/NOW LOADING texture corruption**.
7. **Doubled Press-START, movie hitch, idle→demo.**
8. **60 fps, widescreen, 4:3, XInput.** Then cleanup of debug logging, then final `docs\FINDINGS.md` (see user request).
9. After each run, **report percent complete.** My estimate at handoff: roughly 90% of the title/intro flow and about 60% overall playable (no match reached yet). The last recorded figures (96% / 58%) predate the demo-load and movie-clock fixes.

---

## 10. Reference material

**In the project (local):**
- `docs/ROADMAP.md`, `docs/FINDINGS.md`, `docs/GHIDRA.md`, `docs/PACKAGE_SURVEY.md`, `config/hle_plan.md`, `config/svr06_ps2recomp.toml`, `PS2_PROJECT_STATE.md` (lessons + older status), `README.md`.
- `analysis/`: `asset_index.json`, `elf_summary.json`, `functions.csv`, `table_addrs.txt`, `strings_interesting.txt`, `recomp_log.txt`, `ovl*_pack*.tgz`, `pcsx2_menu_walk_*`, `ref/`.
- `tools/`: `analyze_elf.py`, `index_assets.py`, `mcextract.py`, `check_clean.py`.
- `scripts/`: `AUTO_LOOP.cmd`, `auto_loop.ps1`, `BUILD_AND_RUN.cmd`, `FAST_REBUILD_AND_RUN.cmd`, `run.cmd`, `run_analysis.cmd`, `setup.ps1`, `recomp_hook.ps1`, `link_runtime.ps1`.
- The user's original roadmap/instruction file: `wwe_svr_06_recompilation_roadmap_claude_instructions.md` (source of truth for scope).
- `hctp-recomp-main.zip` in the Claude folder (another PS2 recompilation, useful for comparing runtime solutions).

**Upstream and external (the first is verified from the git remote; the rest are recalled, **verify the URLs**):**
- PS2Recomp (runtime/recompiler this project builds on): https://github.com/ran-j/PS2Recomp (verified: `origin` of `PS2Recomp\`).
- PCSX2 emulator (reference behaviour, snapshots, debugger): https://pcsx2.net and https://github.com/PCSX2/pcsx2.
- Ghidra: https://github.com/NationalSecurityAgency/ghidra; PS2 Emotion Engine support: ghidra-emotionengine-reloaded (https://github.com/chaoticgd/ghidra-emotionengine-reloaded, **unverified**).
- Workflow reference the earlier sessions adopted: hkmodd/ps2-recomp-Agent-SKILL on GitHub (**unverified exact URL**; "reference only").
- PS2 hardware docs: PS2 EE/GS/VU/VIF/DMAC user manuals (PCSX2 repo links and community mirrors), PS2 SDK `sceMc*`, `sceCd*`, `sceSif*` documentation; ps2dev wiki (https://ps2dev.github.io, **unverified**).
- FFmpeg (movie decode: mpeg1video + adpcm_adx): https://ffmpeg.org. raylib (window/render loop): https://www.raylib.com.
- CRI ADX/Sofdec format notes: community documentation (e.g. vgmstream for ADX, https://github.com/vgmstream/vgmstream) (**unverified**).

---

## 11. Practical tips and gotchas (collected)

- Keep device_bash polling loops under 120 s; `grep -c` returning 0 gives exit code 1 (cosmetic).
- Don't list the runner directory (7,000+ generated files) or `grep -r` it; grep specific `sub_XXXXXXXX_0xXXXXXXXX.cpp` files. `grep -h "^ *// 0x"` on a generated file prints the original MIPS disassembly, the fastest way to read game code.
- When the user says "its up" the loop is running; "OPENING" means they're reopening it; "THIS IS VIDEO FREEZE" means the picture froze. If they say "I'll run it for you as admin" they will start the loop in an elevated PowerShell.
- Don't ask the user questions you can answer yourself; they want results. But do tell them what is happening in one sentence when you've been quiet for a while.
- Percent-complete report after each result; no game data to external services; no injected input in builds.

---

## 12. File path index

All paths relative to `C:\Users\ajizz\Desktop\Claude\` (device shell: `~/mnt/Claude/`). Counts are from 2026-10-25. **Never upload anything under `assets\`, `analysis\dumps\`, `analysis\live\*.bin`, `analysis\live\*.png/*.ppm`, `analysis\shots\`, `out\`, or the generated `sub_*.cpp` / `ovlD_*.cpp` files** (game data or code derived from it).

### 12.1 Top level
| Path | What it is |
|---|---|
| `svr06-recomp\` | Our project: config, overrides, scripts, analysis, docs |
| `PS2Recomp\` | User's clone of https://github.com/ran-j/PS2Recomp with our local changes (runtime, IOP, recompiler) |
| `assets\` | Disc contents, one folder per region (see 12.5) |
| `out\` | Generated-code output at the top level (contents not inspected, probably older) |
| `ovlS_new.tgz` | Archive of the newest "S" overlay recompile output |
| `wwe_svr_06_recompilation_roadmap_claude_instructions.md` | The user's original scope/instructions |
| `hctp-recomp-main.zip` | Another PS2 game recompilation, for comparison |
| `WWE SmackDown! vs. Raw 2006 (USA).pdf` | Game manual |
| `wwe-smackdown-vs-raw-2006.9929.max` | Misc (3ds Max file, not used so far) |

### 12.2 `svr06-recomp\` (our project)
| Path | Purpose |
|---|---|
| `README.md`, `PS2_PROJECT_STATE.md` | Overview; lessons learned and (stale) status |
| `docs\ROADMAP.md`, `FINDINGS.md`, `GHIDRA.md`, `PACKAGE_SURVEY.md`, **`HANDOFF.md`** | Documentation; HANDOFF.md is this file |
| `config\svr06_ps2recomp.toml` | Recompiler config: skip ranges (214 entries), stubs, jump tables, patches |
| `config\hle_plan.md` | Plan for HLE of CRI audio/video, SIF etc. |
| `scripts\auto_loop.ps1` / `AUTO_LOOP.cmd` | The build+run loop (section 4) |
| `scripts\run.cmd` | Launcher: kills old `svr06run.exe`, `ps2EntryRunner.exe`, **and PCSX2**; finds `SLES_536.76` under `assets\` (or env `SVR06_ELF`); sets env vars (`SVR06_TRACE`, `SVR06_TURBO=1`, `SVR06_SKIPMOVIES=0`, `SVR06_GSDUMPTICK/LEN`, `SVR06_DUMPDIR`); logs to `analysis\run_log.txt`. The trace/turbo/dump settings are debug leftovers to review before release |
| `scripts\BUILD_AND_RUN.cmd`, `FAST_REBUILD_AND_RUN.cmd`, `link_runtime.ps1`, `setup.ps1`, `run_analysis.cmd`, `runcap.ps1` | Older/manual build and analysis helpers |
| `scripts\recomp_hook.ps1`, `analysis\recomp_overlay.cmd` | Overlay recompile helpers (recompile a RAM-dumped overlay slice with ps2_recomp) |
| `tools\analyze_elf.py`, `index_assets.py`, `mcextract.py`, `check_clean.py` | ELF/asset analyzers; memory-card save extractor; "no game data in repo" check |
| `overrides\svr06_overrides.cpp` | **Main hook file:** watchdog, probes (`pl[]`), `covThunk`, movie/ADX/music hooks, title logic |
| `overrides\svr06_overlay.cpp/.h`, `svr06_funcs.h` | Overlay bank registry (`addBank/addFunc/find`) incl. mid-function resume |
| `overrides\svr06_ovl_demo.cpp` | Registers the demo/overlay banks |
| `overrides\ovlD_*.cpp` (1,168 files) | Generated overlay bank D functions (derived from game code) |
| `overrides\ovl_generated\` | Other generated overlay output |
| `overrides\sub_001A3C28_*.cpp`, `…1A3C88…`, `…1A3E18…`, `…1A3F68…`, `…1A4048…` | Overridden copies of generated functions in the `0x1A3xxx` area (`0x1A3F68` is the sound-slot status poll; the others' exact roles are unverified); they replace the generated runner copies |
| `third_party\ghidra`, `third_party\ghidra-emotionengine-reloaded`, `third_party\PS2Recomp` | Ghidra 12.1.4 + EE extension; a vendored copy of PS2Recomp (the live one is `..\PS2Recomp`) |
| `out\generated`, `generated_r2..r4`, `d5..d9`, `delta_r5..r9.tar.gz`, `ovlB*.tgz`, `ovlS.tar.gz`, `svr06_generated_*.tar.gz` | Generated C++ and history of recompile deltas (derived from game code) |
| `mods\` | Empty; reserved for the mod loader |
| `build\` | Empty |

### 12.3 `svr06-recomp\analysis\` (data and logs)
| Path | Purpose |
|---|---|
| `run_log.txt` | Current game run log (5–6 MB/min; overwritten each launch) |
| `build_log2.txt` | Last build log (~70 MB, check the tail for `BUILD STEP FINISHED`); `build_log.txt` older |
| `auto_done.txt` | Timestamp written when a loop cycle finishes |
| `go.flag` | Touch/append to trigger a rebuild or relaunch |
| `live\cmd.txt` / `out.txt` / `cmd_done.txt` | Live console input / output / ack |
| `live\threads.req` → `threads.txt` | Thread/semaphore dump request/result |
| `live\live_ram.bin`, `live_vram.bin` | Latest `ramdump` / `vramdump` (game data, local only) |
| `live\tex_*.ppm`, `atlas*.png`, `m*.png`, `contact*.png`, `snapsheet.png`, `src_*.png` | Decoded texture/atlas/screenshot studies |
| `live\ref\`, `live\old\` | Reference captures and older dumps |
| `dumps\` | RAM/VRAM dumps: `ram_frame.bin`, `ee_1c0900.bin`, IOP RAM (`iop_ram*.bin`, `iop_slice_*.bin`), overlay slices (`ovl_slice_4d0000_5a2700.bin`, `ovlS_slice_5a2000.bin`), `ram_missing_<addr>_0.bin` (RAM captured when a missing function was hit, one per address), `sdr_code_1a3b00.bin`, `pcsx2_menu_state\` |
| `ovlD\` | Overlay D recompile workspace: `ovlD.elf`, `ovlD.toml`, `ovlD_slice.bin`, `entries.txt`, `ovlD_addrs.txt`, `recomp_build.txt`, `recomp_run.txt`, `out\` |
| `ovlA_pack*.tgz`, `ovlL_pack.tgz`, `ovlM_pack*.tgz` | Packaged overlay A/L/M recompile results |
| `ee_dump_1500/3000/6000/7900.bin`, `gsdump_menu.bin`, `recomp_gs_7000.bin`, `recomp_vram_7000.bin` | Early EE/GS/VRAM dumps at given ticks |
| `t8_*.pgm/.clut` | Decoded 8-bit textures with CLUTs (font/logo palette work) |
| `font_idx.pgm` | Font atlas index dump |
| `asset_index.json` | Disc index: 102 DPAC, 26 AFS, 20 IRX |
| `elf_summary.json`, `functions.csv`, `table_addrs.txt`, `strings_interesting.txt`, `recomp_log.txt` | ELF analysis, function list, jump-table addresses, interesting strings, recompiler log |
| `ref\refsheet0..4.png` | PCSX2 reference screenshots sheets |
| `pcsx2_menu_walk_inputs.txt`, `pcsx2_menu_walk_log.txt`, `pcsx2_menu_walk_reads_summary.txt`, `pcsx2_menu_inputs.p2m2`(+ `_SaveState.p2s`), `pcsx2_emulog_prev.txt` | PCSX2 menu-walk recording (inputs, logs, savestate) |
| `nav*.sh`, `nav*.log` | Older scripted navigation experiments (testing only) |
| `shots\` | Output of the game's own screenshot capture (broken, black) |

### 12.4 `PS2Recomp\` (runtime and tools)
| Path | Purpose |
|---|---|
| `ps2xRuntime\src\main.cpp` | Runner entry point |
| `ps2xRuntime\src\runner\` | ~19,340 generated files: `sub_<addr>_0x<addr>.cpp` per function and `entry_<start>_0x<end>.cpp` split entries. Each has the original MIPS as comments (`grep -h "^ *// 0x" file`). Copies of `overrides\*.cpp` land here |
| `ps2xRuntime\include\ps2_runtime.h`, `ps2_syscalls.h`, `ps2_stubs.h`, `ps2_recompiled_functions.h`, `svr06_funcs.h`, `svr06_overlay.h`, `svr06_prof.h`, `ovlD_functions.h` | Runtime API and our headers |
| `ps2xRuntime\include\runtime\ee_scheduler.h` | Thread status enum and scheduler structs |
| `ps2xRuntime\src\lib\ps2_runtime.cpp` | Function table, `hasFunction/lookup` (uses `svr06ovl::find`), main run loop |
| `ps2xRuntime\src\lib\Kernel\EeScheduler.cpp` (+`.orig`) | **Thread/semaphore/interrupt scheduler** (center of the stall bug) |
| `ps2xRuntime\src\lib\Kernel\Syscalls\Thread.cpp`, `Sync.cpp`, `Interrupt.cpp`, `System.cpp`, `RPC.cpp`, `FileIO.cpp`, `Dispatcher.cpp` | EE syscall implementations; `Dispatcher.cpp` maps syscall numbers |
| `ps2xRuntime\src\lib\Kernel\Stubs\SvrMovie.cpp` | Native movie player (FFmpeg) |
| `…\Stubs\SvrMusic.cpp`, `Audio.cpp` | ADX music and audio output |
| `…\Stubs\MemoryCard.cpp` | HLE `sceMc*` (memory card) |
| `…\Stubs\CD.cpp`, `SIF.cpp`, `RPC.cpp`, `MPEG.cpp`, `IPU.cpp`, `Pad.cpp`, `GS.cpp`, `DMA.cpp`, `Font.cpp`, `SvrConsole.cpp` (probably the live console, unverified), `System.cpp`, `LibC.cpp`, `FileIO.cpp`, `Deci2.cpp`, `VU.cpp`, `TTY.cpp`, `Compatibility.cpp`, `Unimplemented.cpp` | Other HLE modules (`SvrConsole.cpp` name suggests the live console commands live there, unverified) |
| `ps2xRuntime\src\lib\gs\gs_cpu_backend.cpp`, `gs_frontend.cpp`, `ps2_gs_memory.cpp`, `ps2_gif_arbiter.cpp` | Software GS rasterizer, GIF handling, GS memory and texture upload |
| `ps2xRuntime\src\lib\vu\ps2_vu1_core.cpp`, `…_lower.cpp`, `…_upper.cpp`, `ps2_vu1_detail.h` | VU1 interpreter |
| `ps2xRuntime\src\lib\ps2_vif1_interpreter.cpp` | VIF1 packet interpreter |
| `ps2xRuntime\src\lib\ps2_pad.cpp`, `ps2_audio.cpp`, `ps2_memory.cpp`, `ps2_vfs.cpp`, `ps2_iop_host.cpp`, `game_overrides.cpp`, `games_database.cpp` | Pad/XInput, audio, 32 MB memory map, virtual file system, IOP host bridge, per-game override tables |
| `ps2xIOP\src\emulator\`, `iop_module_manager.cpp`, `iop_subsystem.cpp`, `modules\`, `rpc_reply.h` | Emulated IOP (module loading, RPC servers, iop_cdvd) |
| `ps2xRecomp\`, `ps2xAnalyzer\` | Recompiler and ELF analyzer sources |
| `build_svr06b\` | **Active CMake/Ninja build dir**; output `build_svr06b\ps2xRuntime\ps2EntryRunner.exe`; has its own `mc0`, `mc1` (runtime memory card dirs), `imgui.ini` |
| `build_svr06`, `build_recomp`, `build_recomp2` | Older build dirs (do not use) |
| `*.orig`, `*.bak*` files next to sources | Backups of files before our edits (`Thread.cpp.bak_ret`, `gs_cpu_backend.cpp.bak_tex`, `ps2_gif_arbiter.cpp.bak`, `System.cpp.orig`, `EeScheduler.cpp.orig`) |

### 12.5 `assets\` (never upload)
- `assets\@WWE SMACKDOWN! VS. RAW 2006 (EUROPE, AUSTRALIA).ISO\` is the **PAL disc contents the project runs on**: `SLES_536.76` (boot ELF), `SYSTEM.CNF`, `MODULE\` (IOP IRX), `BIN\`, `PAC\` (`M.PAC` character/profile records, `PACLISTP.H`), `ADDLISTP.H`, `BGM\*.AFS` (CRI ADX music, ENV, announcer, crowd, SFX archives), `MOVIEP\` (LOGOP, OPP, ENDP, NMVP000–004 `.AFS` with `.sfd` movies), `NTGUI2\`, `BELT\`, `ED0..ED6\`, `THMCAB/THMCAS/THMLOC`, `mc0\BESLES-53676SD7\` (host memory card save dir), `mc1\`, and `…-FILELIST-LSN.TXT` (LBN file list).
- Other regions (not yet supported): `…(USA)`, `…(KOREA)`, `…(ITALY)`, `…SmackDown! vs. Raw 2006 (Japan)`.
- `assets\PS2 ISO Rebuilder v3.2.exe` is a user utility.

### 12.6 Indexes (where to look things up)
| Question | Look here |
|---|---|
| What does the game do at address X? | `grep -h "^ *// 0x" PS2Recomp\ps2xRuntime\src\runner\sub_00XXXXXX_0xXXXXXX.cpp` (use the function containing X: nearest lower file name); for overlay code `analysis\ovlD\ovlD_addrs.txt`, `entries.txt`, or `overrides\ovlD_*.cpp` |
| List of known functions | `analysis\functions.csv`, `analysis\table_addrs.txt` |
| What files are on the disc? | `analysis\asset_index.json`, the `…-FILELIST-LSN.TXT` in the ISO folder |
| Strings in the ELF | `analysis\strings_interesting.txt` |
| What the real game does visually | `analysis\ref\refsheet*.png`, `C:\Users\ajizz\Documents\PCSX2\snaps`, `analysis\pcsx2_menu_walk_*` |
| Where is function Y called? | `grep -l "func_YYYYYY" sub_00*.cpp` inside the runner dir with `timeout` (slow: about 90 s) |
| Syscall numbers → implementations | `…\Kernel\Syscalls\Dispatcher.cpp` and the wrapper table at EE `0x126BC0` |
| Missing-function events | run log lines from `reportMissingFunction` (exact tag unverified; grep for `missing`), plus `analysis\dumps\ram_missing_<addr>_0.bin` for the RAM at the time |
| Movie/ADX hook messages | run log tags `[movie]`, `[mvobj]`, `[mend]`, `[music]` |
| Stall diagnosis | run log `[watchdog]` blocks, `live\threads.txt` |
