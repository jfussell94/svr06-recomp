# Game completion tracker (weighted)

Updated 2026-11-07. Source: tools/game_completion.json -> run tools/game_completion.py.

code = recompiled code for this feature matches the PS2 game (audited/verified vs reference); visual = renders correctly; audio = sounds/music correct; input = controls work; logic = game rules/state machines verified against the original; playable = can be used start-to-finish without bugs/crashes/hangs. Each component is 0-100, scored from evidence only (see 'evidence'). Overall for a feature = weighted sum; group = weighted by 'size'. A component set to null does not apply to that feature and is left out of its weighting.

Weights: code 25%, visual 20%, audio 10%, input 10%, logic 20%, playable 15%

## OVERALL: 37% ###.......

| Group | Size weight | % | Bar |
|---|---|---|---|
| Boot / intro / title | 4 | 70% | #######... |
| Main menu & submenus | 8 | 43% | ####...... |
| Exhibition (match setup) | 10 | 38% | ###....... |
| Match types (in-ring) | 22 | 22% | ##........ |
| Match engine subsystems | 20 | 33% | ###....... |
| Entrances | 8 | 65% | ######.... |
| Season / GM mode | 8 | 32% | ###....... |
| Create modes | 8 | 26% | ##........ |
| Audio | 6 | 48% | ####...... |
| Platform layer | 6 | 53% | #####..... |

### Boot / intro / title

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Boot, logos, intro movies | 79% | 90 | 85 | 20 | 70 | 90 | 85 | boots to title; movies decode; sound model new/unverified |
| Title screen / Press START | 72% | 85 | 70 | 20 | 70 | 85 | 75 | Press START layering bug open; title to demo load now completes (~8 min load, was stalled) |
| Memory card screen | 59% | 60 | 80 | 20 | 70 | 50 | 60 | screen shows; saves partial |

### Main menu & submenus

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Main menu navigation | 66% | 75 | 65 | 20 | 70 | 75 | 70 | navigable in logged runs; backgrounds show stripe garbage |
| Options (audio/controller/game settings) | 46% | 50 | 60 | 10 | 60 | 40 | 40 | not exercised |
| Save / Load | 40% | 40 | 60 | 10 | 60 | 30 | 30 | memory card partial |
| Shop / Unlockables / Unlock All | 31% | 30 | 50 | 10 | 60 | 20 | 15 | not exercised |
| Records / Hall / Stats screens | 31% | 30 | 50 | 10 | 60 | 20 | 15 | not exercised |

### Exhibition (match setup)

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Wrestler select / roster | 41% | 50 | 40 | 10 | 60 | 40 | 35 | resource namespace catalogued; selection not verified |
| Arena select | 34% | 40 | 35 | 10 | 60 | 30 | 25 | not verified |
| Match type / rules screens | 40% | 45 | 50 | 10 | 60 | 35 | 30 | not verified |

### Match types (in-ring)

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Singles | 26% | 35 | 30 | 5 | 40 | 25 | 15 | demo/attract runs; collision primitive fixed (unverified in play) |
| Tag team | 22% | 30 | 25 | 5 | 35 | 20 | 10 | not exercised |
| Triple threat / Fatal 4-way / 6-man | 22% | 30 | 25 | 5 | 35 | 20 | 10 | not exercised |
| Handicap / Battle royal / Royal Rumble | 21% | 28 | 25 | 5 | 35 | 18 | 8 | not exercised |
| Cage / Hell in a Cell / Elimination Chamber | 20% | 28 | 22 | 5 | 35 | 15 | 8 | not exercised |
| Ladder / TLC / Table / Iron Man / Last Man / Submission | 20% | 28 | 22 | 5 | 35 | 15 | 8 | not exercised |

### Match engine subsystems

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Skeleton / pose / animation playback | 68% | 75 | 65 | - | - | 70 | 55 | skeleton, euler->matrix, animation applier and controllers mapped; entrance poses render; crouched legs ISS-139 open |
| Collision / hit detection | 41% | 45 | - | - | - | 40 | 35 | pair query, overlap test and response dispatch verified in a run; response fn 0x5F9C40, ring/rope bounds, sqrtf fix (ISS-141) not verified in play; ISS-139 legs open |
| Grapple / strike / reversal logic | 15% | 20 | - | - | 30 | 10 | 5 | not traced |
| Camera | 33% | 35 | 40 | - | - | 30 | 25 | clip classifier flags fixed; unverified in match |
| Physics / ring / ropes | 25% | 30 | 30 | - | - | 20 | 15 | FPU rounding differs by 1 ULP (opt-in fix) |
| Crowd / referee / AI | 14% | 20 | 20 | 5 | - | 10 | 8 | not traced |

### Entrances

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Entrance playback (movie+music+walk) | 56% | 60 | 65 | 45 | 60 | 55 | 40 | runs in real match entrance: black screen (CL-0096), textures (ISS-130), HUD fixed; wrestler/walk/music play; VU1-bound so slower than full speed (ISS-144); custom song in real entrance unconfirmed | 11-07: freeze root cause found (ISS-147 malformed MoHo node -> NULL skeleton -> jump 0x3F800000), guard coded but not yet built/verified; scores unchanged |
| Create entrance / Entrance set-up menu | 75% | 80 | 80 | 70 | 75 | 70 | 70 | menu works; custom music list (303 themes, A-Z, year tags) and refresh work; slot limit 165 vs 303 and save width >159 unverified |

### Season / GM mode

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| GM desk and menus | 42% | 45 | 55 | 15 | 60 | 35 | 35 | GM Desk reached |
| Season progression / calendar / PPV | 21% | 25 | 30 | 5 | 40 | 15 | 10 | not exercised |

### Create modes

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Create-a-Wrestler | 29% | 35 | 35 | 5 | 50 | 25 | 20 | Name Engraving screen reached |
| Create belt / finisher / other | 22% | 25 | 30 | 5 | 50 | 15 | 10 | not exercised |

### Audio

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Music (ADX streaming, soundtrack) | 54% | 65 | - | 40 | - | 55 | 45 | custom music system (own list, 303 extracted themes, year tags) working; entrance music plays; stock ADX/level accuracy not re-verified |
| SFX / commentary / crowd (SPU2) | 41% | 60 | - | 20 | - | 40 | 25 | bank uploads, voices model in place; no KON of real samples seen yet; stream audio present in dump |

### Platform layer

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Rendering (software GS) | 62% | 70 | 60 | - | - | 60 | 55 | garbled demo geometry ISS-101, halftone |
| Input (keyboard/XInput) | 45% | 60 | - | - | 30 | 40 | 35 | keyboard works; XInput pending |
| Performance / windowing | 53% | 60 | - | - | - | 50 | 45 | 60 FPS locked on intros/title (CL-0112-0115); entrances ~65% of game thread in VU1, async worker (CL-0127) under test | 11-07: launcher v1 (build progress bar), control channel (heartbeat/shot/perf/health), scenario runner added; not scored |

Scores are conservative: playable/logic stay low until a feature has been exercised in a run and its code verified against the original.