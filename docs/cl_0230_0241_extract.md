## CL-0230 - Native VU1 generator: inline ILW / ISW / FCAND (2026-11-14)
- tools/vu1bench/gen.cpp (.bak_inlinelo): lower ops ILW (0x04), ISW (0x05, via vu.queueStore) and FCAND (0x12) are emitted inline instead of calling vu.execLower. Generic lower calls in the 59 native programs: 909 -> 508.
- Verified: rbench differential test on 6 captured images x 150 seeds (4800 runs each): 0 native-vs-interpreter diffs, 0 shadow mismatches (check build).
- Installed into PS2Recomp/ps2xRuntime/src/lib/vu/ps2_vu1_native_gen.cpp (backup .bak_inlinelo). Local only, never publish.
- Needs rebuild; expected effect: less worker-thread time per VU1 run. Measure match/entrance vsync/s afterwards.

## CL-0231 - sceGsSyncPath(0) no longer fences the VIF1/GIF worker (2026-11-14)
- Entrance profile of the GAME thread (sampler follows the 2nd-busiest thread when analysis/live/samp_second.txt exists; hot plugin .bak_v14): 39% of game-thread time was inside svr06VifFence called from sceGsSyncPath; the worker itself was ~1/3 idle, so the game thread was blocking on the worker, not the reverse.
- Kernel/Stubs/GS.cpp (.bak_syncfence, PAL+USA): mode 0 keeps processPendingTransfers() but skips ::svr06VifFence() (DMA completes at the kick; the worker consumes a copy; all worker-owned state readers have their own fence sites). SVR06_SYNCFENCE=1 restores the old fence.
- Also seen: 13% of game-thread time in gsvk::gifTransfer -> GSInterface::init_transfer -> GSRenderer::wait_timeline (direct, non-worker GIF submit waiting on the GPU). Next candidate.
- Needs rebuild. Watch for: missing/garbled GS output after SyncPath (then use SVR06_SYNCFENCE=1 to confirm).

## CL-0232 - sceGsSyncPath(0): skip the GIF register polls when the async worker is on (2026-11-14)
- Game-thread profile after CL-0231: still 40% of game-thread time in svr06VifFence, now via readIORegister(0x10003C00 / 0x10003020) in sceGsSyncPath's polling loops (fence site 7).
- Kernel/Stubs/GS.cpp (.bak_syncfence2, PAL+USA): with async worker on (flag 15) mode 0 returns 0 right after processPendingTransfers(). Mode 1 (status query) unchanged. SVR06_SYNCFENCE=1 restores the old behaviour.
- Needs rebuild; measure entrance vsync/s again (Taker entrance read ~40 before this).

## CL-0233 - GS submit queue thread + faster GIF scan; continuous multi-thread sampler (2026-11-14)
- Match profile with the new multi-thread sampler (hot plugin v15, .bak_v15; writes analysis/live/samp_t0..4.txt + samp_threads.txt every ~20 s): busiest thread = VIF1/GIF worker at 88% of a core (VU1 ~55%, of which program 0x28b8 = f20 ~29%; GIF->paraLLEl-GS ~25%, gifTransfer's own icon/texture-pack scan ~12% leaf). Game thread 23%, presenter 19%. The worker caps the match at ~35-40 fps.
- gs/gs_vulkan_bridge.cpp (.bak_gsq, PAL+USA): new gsq queue. gsvk::gifTransfer called on the VIF worker copies the packet into a queue (pooled buffers, 48 MB back-pressure) and a dedicated thread feeds paraLLEl-GS. Ordering: writeReg / uploadImage / presentFrame / readLocalToHostCT32 / readVramPages / dumpVram and any non-worker gifTransfer drain the queue first. SVR06_GSQ=0 disables it.
- ipStream (icon patch / texture pack GIF scanner): data blocks without an IMAGE upload of interest are skipped in one step instead of one qword per loop.
- ps2_memory.cpp (.bak_gsq, PAL+USA): svr06VifFence also drains the GS queue; svr06VifIdle (presenter) requires the GS queue empty too. include/runtime/gs/gs_vulkan_bridge.h: drainQueue(), queueIdle().
- Ordering test of the queue logic (standalone, 200k packets from a worker thread + one non-worker submit): order preserved.

## CL-0234 - Game's own sceGsSyncPath (0x1A6F20) routed to the HLE stub (2026-11-14)
- Match after CL-0233: 42-46 fps (was ~35-40). Thread loads: worker 77%, game thread 66%, GS submit thread 41%, presenter 17%.
- Game-thread profile: 36% inside svr06VifFence, reached from sub_001A6F20 -> Load32 -> readIORegister. 0x1A6F20 is the game's statically linked libgraph sceGsSyncPath (polls 0x10009000, 0x1000A000, 0x10003C00, 0x10003020); the VIF/GIF register reads fence the worker on every poll.
- overrides/svr06_overrides.cpp (.bak_gssync): replaceFunction(0x1A6F20, ps2_stubs::sceGsSyncPath) (stub has no worker fence, CL-0231/0232). SVR06_NOGSSYNCHLE=1 keeps the recompiled function. PAL only (USA address not mapped yet).

## CL-0235 - sceGsSyncPath mode 1 (status poll) skips the VIF1/GIF status reads with the async worker (2026-11-14)
- After CL-0234 (build 04:57 UTC): match 30-47 fps; thread loads worker 61%, GS queue 31%, game 14%, presenter 12%: nothing saturated, the game thread waits.
- Game-thread profile: 47% in svr06VifFence inside ps2_stubs::sceGsSyncPath -> readIORegister: the game polls sceGsSyncPath(1) and the VIF1_STAT/GIF_STAT reads fence the worker every time.
- Kernel/Stubs/GS.cpp (.bak_syncmode1, PAL+USA): with the async worker on, mode 1 reports only D1/D2 CHCR busy bits; VIF1/GIF count as idle. SVR06_SYNCFENCE=1 restores the reads.

## CL-0236 - No worker fence on GS CSR reads with Vulkan GS; VIF fence no longer drains the GS queue (2026-11-14)
- After CL-0235 (build 05:07 UTC): match 31-47 fps; worker 70%, GS queue 61%, game 42%. Game-thread profile: 49% in svr06VifFence via PS2Memory::read64 -> isGsPrivReg: the game polls GS CSR.
- With paraLLEl-GS, CSR is never fed by the GIF stream (FINISH/SIGNAL bits are only set by the software GS; VSYNC/field come from EeScheduler), so the fence bought nothing; and since CL-0233 each fence also waited for the GS queue (GPU submits).
- ps2_memory.cpp (.bak_csrfence, PAL+USA): isGsPrivReg skips the fence for CSR (0x12001000) when gsvk::enabled(); svr06VifFence no longer calls gsvk::drainQueue() (gsvk entry points drain themselves; the presenter idle check still includes the queue).

## CL-0237 - Frame throttle between the game thread and the VIF1/GIF worker (2026-11-14)
- After CL-0236 (build 05:18 UTC): the game thread no longer waits on the worker at all; it ran ahead (vsync ~80/s while only 15-18 frames/s were shown, then stalls with vsync frozen); worker at 97%. Stutter instead of speed.
- ps2_memory.cpp (.bak_throttle, PAL+USA): job tickets (enq/done) in the worker; svr06VifFrameThrottle(vsyncTick): once per vsync, the caller waits (max 200 ms) until the worker has finished everything that was queued one vsync earlier. One frame of pipelining: the game builds frame N+1 while the worker renders frame N.
- Kernel/Stubs/GS.cpp (.bak_throttle, PAL+USA): sceGsSyncPath mode 0 and mode 1 call the throttle when the async worker is on.

## CL-0238 - Presenter latch waits for the finished frame, not for an idle worker (2026-11-14)
- After CL-0237 (build 05:32 UTC): steady 17-19 fps with every thread mostly idle (worker 19%). Cause: the presenter (ps2_runtime.cpp) waited until "no draw for 4 ms + MTGS idle + worker idle" (cap 40 ms); with one frame of pipelining the worker is never idle, so every present hit the 40 ms cap.
- ps2_memory.cpp (.bak_latch, PAL+USA): g_svr06FrameTicket = worker job count at the game's last frame boundary (set by the CL-0237 throttle); svr06VifFrameDone() = worker finished those jobs.
- ps2_runtime.cpp (.bak_latch, PAL): with the async worker on, the latch waits only for svr06VifFrameDone() (max 40 ms); the old idle test stays for the synchronous path and the movie path. USA runtime has no presenter idle wait (unchanged).

## CL-0239 - PAL vblank 50 Hz; no vsync catch-up bursts (2026-11-14)
- User: "smoother but it looked too fast". Found: Kernel/EeScheduler.cpp paced the emulated vblank at 16667 us (NTSC 60 Hz) in the PAL build. The PAL game advances its logic per vsync, so at full speed it ran 20% fast; earlier this was hidden because the game never reached full speed. Also, after a slowdown the scheduler ran missed vsyncs back to back (~80 vsync/s bursts).
- EeScheduler.cpp (.bak_catchup): PAL tree kVBlankPeriod / kVBlankPeriodCycles = 20000 us (50 Hz). USA tree stays 60 Hz (NTSC). Both trees: when the next vblank is more than two periods behind wall-clock time, pacing restarts from now (no catch-up burst).

## CL-0240 - Presenter latch ticket = previous frame boundary (2026-11-14)
- After CL-0238/0239 (build 05:42 UTC): vsync ~45-55/s (correct PAL speed) but still 14-18 presented fps, worker 95%. The latch ticket was the NEWEST frame boundary; the throttle updates it before waiting, so with a busy worker "frame done" was never true and every present hit the 40 ms cap.
- ps2_memory.cpp (.bak_latch2, PAL+USA): g_svr06FrameTicket = previous boundary (prevT): the frame the worker has to finish before the game may continue.

## CL-0241 - Game-thread GIF submits also go through the GS queue (2026-11-14)
- After CL-0240 (build 05:52 UTC): match ~50 presented fps, game ~44 of 50 vsync/s. Entrance (05:56): ~34-45 vsync/s, no thread saturated (game 50%, worker 48%, GS queue 39%).
- Entrance game-thread profile: 27% sceGsExecStoreImage (VRAM save: worker fence + GPU readback, needed for correctness, ISS-122/130), 22% synchronous gsvk::gifTransfer from the game thread waiting in ParallelGS init_transfer -> wait_timeline (GPU).
- gs/gs_vulkan_bridge.cpp (.bak_gsqall, PAL+USA): with the async worker on, gifTransfer from any thread is queued (not only from the worker). Readbacks still drain the queue first. SVR06_GSQ=0 still turns the queue off.
- Result (build 06:04 UTC, entrance): vsync exactly 50/s over 35 s (full PAL speed), ~60 presented fps, thread loads 64/52/48/16%. Match (CL-0240 build): ~50 presented fps, game ~44-50 vsync/s.
