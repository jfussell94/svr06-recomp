# Game completion tracker (weighted)

Updated 2026-11-10. Source: tools/game_completion.json -> run tools/game_completion.py.

code = recompiled code for this feature matches the PS2 game (audited/verified vs reference); visual = renders correctly; audio = sounds/music correct; input = controls work; logic = game rules/state machines verified against the original; playable = can be used start-to-finish without bugs/crashes/hangs. Each component is 0-100, scored from evidence only (see 'evidence'). Overall for a feature = weighted sum; group = weighted by 'size'. A component set to null does not apply to that feature and is left out of its weighting.

Weights: code 25%, visual 20%, audio 10%, input 10%, logic 20%, playable 15%

## OVERALL: 42% ####......

| Group | Size weight | % | Bar |
|---|---|---|---|
| Boot / intro / title | 4 | 73% | #######... |
| Main menu & submenus | 8 | 43% | ####...... |
| Exhibition (match setup) | 10 | 53% | #####..... |
| Match types (in-ring) | 22 | 29% | ##........ |
| Match engine subsystems | 20 | 39% | ###....... |
| Entrances | 8 | 68% | ######.... |
| Season / GM mode | 8 | 32% | ###....... |
| Create modes | 8 | 26% | ##........ |
| Audio | 6 | 48% | ####...... |
| Platform layer | 6 | 49% | ####...... |

### Boot / intro / title

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Boot, logos, intro movies | 79% | 90 | 85 | 20 | 70 | 90 | 85 | boots to title; movies decode; sound model new/unverified |
| Title screen / Press START | 76% | 88 | 70 | 20 | 70 | 90 | 85 | NOW LOADING hang fixed (CL-0180, verified ~4 h of play); Press START layering bug still open |
| Memory card screen | 64% | 65 | 80 | 20 | 70 | 60 | 70 | per-version card folders; launcher card picker (11-10); saves still partial |

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
| Wrestler select / roster | 57% | 65 | 60 | 10 | 60 | 65 | 60 | used constantly in testing; any-gender titles verified by user 11-10 (CL-0173) |
| Arena select | 49% | 55 | 55 | 10 | 60 | 50 | 50 | arenas load and play in matches |
| Match type / rules screens | 52% | 60 | 60 | 10 | 60 | 55 | 50 | title matches incl. men/women titles and mixed tag titles verified by user 11-10 |

### Match types (in-ring)

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Singles | 48% | 55 | 55 | 5 | 60 | 50 | 45 | matches played by user; speed ~17 of 50 game frames/s (CL-0182); limb IK fixed 11-10 |
| Tag team | 43% | 50 | 50 | 5 | 55 | 45 | 35 | mixed man+woman tag titles verified in Exhibition 11-10; in-ring tag play lightly exercised |
| Triple threat / Fatal 4-way / 6-man | 22% | 30 | 25 | 5 | 35 | 20 | 10 | not exercised |
| Handicap / Battle royal / Royal Rumble | 21% | 28 | 25 | 5 | 35 | 18 | 8 | not exercised |
| Cage / Hell in a Cell / Elimination Chamber | 20% | 28 | 22 | 5 | 35 | 15 | 8 | not exercised |
| Ladder / TLC / Table / Iron Man / Last Man / Submission | 20% | 28 | 22 | 5 | 35 | 15 | 8 | not exercised |

### Match engine subsystems

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Skeleton / pose / animation playback | 81% | 85 | 80 | - | - | 80 | 75 | leg IK crouch root cause fixed (VSQI/VSQD operand swap, CL-0181), user-confirmed 11-10; arm IK deltas may be fixed too |
| Collision / hit detection | 51% | 55 | - | - | - | 50 | 45 | limb misalignment fix expected to restore grab/strike registration; needs more in-match testing |
| Grapple / strike / reversal logic | 33% | 35 | - | - | 45 | 30 | 25 | registering better after IK fix; not traced in code; auto-reversal open |
| Camera | 33% | 35 | 40 | - | - | 30 | 25 | clip classifier flags fixed; unverified in match |
| Physics / ring / ropes | 25% | 30 | 30 | - | - | 20 | 15 | FPU rounding differs by 1 ULP (opt-in fix) |
| Crowd / referee / AI | 14% | 20 | 20 | 5 | - | 10 | 8 | not traced |

### Entrances

| Feature | Total | code | visual | audio | input | logic | playable | Evidence |
|---|---|---|---|---|---|---|---|---|
| Entrance playback (movie+music+walk) | 60% | 65 | 70 | 45 | 60 | 60 | 50 | limb fix + rebuild awaiting user test 11-10 |
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
| Performance / windowing | 41% | 60 | - | - | - | 30 | 25 | CORRECTION: fps readout = host presents; game itself runs ~17 of ~50 vsync/s in-ring, single thread saturated (CL-0182). getenv fix + lazy flags built; threading plan open |

Scores are conservative: playable/logic stay low until a feature has been exercised in a run and its code verified against the original.