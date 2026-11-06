# Visual reference (YouTube 6Uh0FCpnYsI, PS2 SvR 2006 gameplay, 13:10) - 2026-10-28

Frames sampled in the built-in browser (timestamps = video time). Use these as the target look when judging our render (analysis\shots).

| Video time | What it shows | Target look / things to check in our render |
|---|---|---|
| ~0:10-1:19 | Intro logo animation (blue particles on black), then THQ/JAKKS/GameSpy legal text | clean black bg, no stripe garbage, text crisp |
| 2:38 | NOW LOADING: SmackDown vs RAW 2006 logo bottom right, black background, [music] playing | logo bottom-right, solid black bg; our loading screen draws the same but is ~8 min long |
| 3:57 | Match HUD: wrestler names top corners (Edge COM left, Jericho 1P right), crowd backdrop, red ropes, timer bottom-right (00:27), referee, announcer captions | crowd = flat billboard rows, ropes red/white, ring apron skirt, HUD name plates with blue health/spirit bars; timer digits italic |
| 5:16 | Entrance: wrestler (Edge) walking toward stage, blue/white spot beams through haze, stage truss | volumetric light cones (additive blended quads), dark arena, bloom-less |
| 7:54, 10:32 | In-ring action, mat texture with logo, wrestlers fully textured/skinned, commentary audio (Cole/Lawler style lines) and crowd noise | skeleton animation, skin textures, mat decals; audio: commentary + crowd bed + hit SFX |

## Differences vs our current render (to prioritise)
1. Match geometry: ref shows clean skinned wrestlers; ours (shot_67) is garbled -> ISS-101 (VU1 skinning/GIF path) is top visual priority.
2. Backgrounds: ref has solid black/clean gradient; ours has stripe garbage on menu backgrounds.
3. Halftone glyphs: ref text is solid; ours halftone (alpha/texture-format issue).
4. Press START layering: ensure text over logo, not under.
5. Audio targets from the video: continuous crowd noise, commentary lines during match, hit/impact SFX, entrance music; ours only produced the ADMA stream so far.
6. Loading screen: ref load is a few seconds; ours ~8 min (VU1 interpretation cost).

No pixel-diff has been run yet; this is a qualitative reference. Next: capture our match frame at the same camera and compare.
