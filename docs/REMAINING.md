# What is needed to reach 100% per area (refreshed 2026-10-28)

| area | now | done when | blocked by |
|---|---|---|---|
| Math library audit | 95 | exact libvu0 stubs verified at runtime | needs a run (not allowed yet) |
| Vector-math translator | 93 | U/O flag precision (1898 flag-only cases), special-value (exp255/denormal) 336 cases, verify in a run | a run |
| FP translator | 100 | done: hardware-verified (ps2autotests 901/901) + every game COP1 word vs hardware model (CL-0110) | none |
| Integer/MMI translator | 100 | done (model-verified); confirm blend/dot outputs in a run | none |
| Other libraries | 65 | Gs pixel-level checks, sceGsSyncV return value | rendering work |
| Skeleton/pose | 60 | keyframe playback mapped, VU0 programs verified | VU0 micro harness |
| Collision | 15 | broadphase/response mapped, overlap fix verified in a match | static tracing + a run |
| Entrances/flow | 16 | /CENT sequence and match state machine mapped | static tracing + match-state dump |
| Music | 30 | song select table mapped, timer-seed fix verified | one run |
| Memory map | 50 | global tables labelled | state dump + tracing |
