# Completion-rate tracking

The user wants a completion percentage reported with every rebuild/result. Format used in every progress message: `Status: ~NN% intro/title, ~NN% playable`.

## 1. Reported history (facts)
| Date | Source | Intro/title | Playable | Notes |
|---|---|---|---|---|
| before 2026-10-25 | `PS2_PROJECT_STATE.md` | ~96% | ~58% | no match reached; demo-load and movie-clock fixes pending |
| 2026-10-25 | HANDOFF estimate | ~90% | ~60% | "no match reached yet"; figures above predate the demo-load and movie-clock fixes |
| 2026-10-26 evening | status doc | ~95% | ~65% | all 10 overlay banks installed; black screen after the title movie was the open item |
| 2026-10-27 (after branch/entry/label/heap fixes) | session | ~96% | ~72% | after Start the loading completes and a 3D arena scene renders at 60 fps (cage, crowd, lights, wrestlers); the demo/match freezes after the load (ISS-100) |
| 2026-11-01 night (after CL-0098) | session | ~96% | ~75% | Vulkan (paraLLEl-GS) is the player renderer; Locker Room reachable and drawn but with black boxes; VU1 stuck-program count cut ~600 -> ~50 per 3 s, VU1 cost 50 -> 37 ns/cycle; weighted feature tracker ~35% (was 33% on 10-28) |
| 2026-11-02 07:28 (after CL-0100) | session | ~96% | ~78% | VU1 JR/JALR fix: Locker Room renders like PCSX2, 0 runaways, 0 xgfail; attract cage demo 60 FPS; one NOW LOADING hang after a demo (ISS-134) |
| 2026-11-10 23:10 (after CL-0180..0182) | session | ~97% | ~82% | NOW LOADING hang fixed (CL-0180); any-gender titles verified (CL-0173); leg-IK root cause fixed (CL-0181, user-confirmed); matches played; weighted tracker 42% (was 37% on 11-07). Real game speed in-ring ~17 of ~50 vsync/s - the fps readout was misleading |

Next report will use the same two numbers; update this table in the same commit as the changelog entry (`CL-nnnn`) that caused the change.

## 2. How the numbers are produced (rubric, so they can be compared over time)
Intro/title percentage = share of this checklist that works like PCSX2 (equal weights):
1. Boot to Yuke's logo - done
2. Logo movie plays, natural end - done
3. Brand video plays, natural end, Start skips - done (hitch on a black frame is open)
4. Title movie plays and holds last frame until Start - done
5. Press START screen: background, logo - matches PCSX2; text glyph boxes - OPEN; pill bar - OPEN; single Start press - OPEN
6. Idle -> demo starts by timeout, no injected input - works (HUD banner missing, ISS-104)
7. Memory-card autoload screen text - done
8. Audio (title music once, no doubling) - OPEN

Playable percentage = estimated share of the full-game checklist that is playable end to end; treat as a rough guide, re-estimated per session:
| Area | Est. | Basis |
|---|---|---|
| Kernel/scheduler/boot | 96% | boots reproducibly; NOW LOADING hang fixed (CL-0180); remaining: window drag pause |
| Recompiled code coverage (main + 10 overlay banks) | 92% | entry/label/hidden-start classes fixed; `dnas.bin` intentionally not recompiled; more missing-targets may appear in untested modes |
| Movies | 90% | logo/brand/title OK; Titantrons and ending untested |
| GS rendering | 84% | geometry matches PCSX2; edge-dash glitch ISS-158 open |
| VU1/VIF rendering | 82% | VSQI/VSQD fix (CL-0181); interpreter is ~2/3 of the game thread |
| Match load and play | 70% | matches load and play (user-verified); limbs fixed 11-10; slow (~17/50 game fps) |
| Audio | 48% | custom entrance music works; SFX/commentary unverified |
| Input | 55% | keyboard + P2 mapping work; XInput/remap incomplete |
| Memory card / saves | 55% | per-version card folders + launcher card picker; round trip partly verified |
| Menus (front end), Create-a-Superstar, etc. | 60% | Exhibition setup, entrance set-up, shop in use; Create modes lightly exercised |
| Performance / real game speed | 35% | CORRECTION: fps readout = host presents; game runs ~17 of ~50 vsync/s in-ring; game thread saturated (CL-0182) |
| Extras (launcher, settings, cheats, custom music, texture packs, icon sets) | 60% | launcher v2 (cards, cheats, games), in-game settings, custom music, texture packs built; widescreen/importers open |
These are estimates by the assistant, last re-estimated 2026-11-10, not measurements.

## 3. Milestones
| Milestone | Status | Date |
|---|---|---|
| Boot to logo | done | 2026-10-21/22 |
| Native movie player | done | 2026-10-23 |
| Overlay system + all 10 banks | done | 2026-10-26 |
| Main thread survives Start and loads | done | 2026-10-27 |
| Arena scene renders (cage, crowd, wrestlers) | done | 2026-10-27 |
| Heap fits like the real console | done | 2026-10-27 |
| Text/HUD passes match PCSX2 | open | |
| Match runs (input, AI, clock) | open | |
| Memory-card round trip | open | |
| Audio complete | open | |
| 60 FPS steady, window drag safe | open | |
| 1:1 verification tooling (coverage + PCSX2 state/GS comparison) | open | |
| Launcher, in-game settings, presets | planned | |

## 4. Per-session log
- 2026-10-26: overlay banks installed, thread logging, JAL/PMFHL fixes (CL-0010..0016); 95% / 65%.
- 2026-10-27: S64 branches, entry points, labels, hidden starts, heap cap (CL-0017..0021); instrumentation for corruption and glyphs (CL-0022..0025); documentation set (CL-0026); 96% / 72%.
- 2026-11-01 night: runaway-loop safeguard, VU1 speedups, xgfail/mpg/vif diagnostics (CL-0098); 96% / 75%; weighted tracker ~35%.

- 2026-11-02: VU1 JR/JALR stale-VI fix (CL-0100), Locker Room correct; 96% / 78%.

- 2026-11-03: ISS-130 texture fix (CL-0107), limb-flail checkpoint fix (CL-0108), P2/controller mapping (CL-0109); 96% / 79%; weighted tracker 32.7%. Input area 40% -> 55% (mapping built, unverified in play).
- 2026-11-03 07:01 build: EE FPU bit-exact with PS2 hardware + SQRT.S operand fix (CL-0110, ISS-141/142); FPU (COP1) translator audit 97% -> 100%. Build OK (full rebuild, linked 07:01).

- 2026-11-10: re-estimated area table (assistant estimates, not measurements): Kernel/boot 96, Recompiled code coverage 92, Movies 90, GS rendering 84, VU1/VIF 82, Match load and play 70, Audio 48, Input 55, Memory card/saves 55, Menus 60, Performance 35 (corrected: game thread saturated, ~17/50 game fps in-ring), Extras (launcher/settings/cheats/music/texture packs) 60. Headline 97% / 82%.
