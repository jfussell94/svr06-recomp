# ENTRANCE / TITANTRON / MUSIC / ARENA DATA AUDIT (2026-11-01)
Offline audit of disc data (EU/AUS ISO) plus the roster table read from guest RAM (live_ram.bin, 160-byte records at 0x3d9c26).

## 1. Roster record layout (0xA0 bytes, decoded)
| Off | Meaning (confidence) |
|---|---|
| +0x00 | display name ; +0x5C short name |
| +0x77 | class/size byte (0..5; 0 = women, 5 = Andre) |
| +0x78 | wrestler index (= roster idx) |
| +0x7A / +0x7C | u16 ids = idx*10+1 and idx*10+2 (two per-wrestler resource ids) |
| +0x7E | 03 wrestler, 06 non-wrestler (referee/announcer/staff) |
| +0x7F/+0x80 | optional alt pair (ff ff = none). Only Austin 3,4 / HHH 5,6 / JBL 7,8 / Cena 9,10 / Carlito 11,12 / Kane 13,14 have one: sequential extra-asset slots (likely entrance props/alt gear) |
| +0x87/+0x88 | music pair (shared by teams/families: Edge+Christian 53,118; Bashams 34,65; Hogans 4,99; Eddie+Chavo 42,83; Conway+Grenier 53,119; Undertaker+Booker T 42,79) |
| +0x8A | unique 0..77 select-screen sort order (alphabetical by displayed name ignoring "The") |

## 2. Roster table
idx | name | cls | alt pair | music pair (+87,+88) | sort (+8A)
---|---|---|---|---|---
0 | The Rock | 3 | - | 8,24 | 56
1 | Steve Austin | 3 | 3,4 | 42,115 | 57
2 | Triple H | 3 | 5,6 | 6,100 | 42
3 | Undertaker | 4 | - | 42,79 | 43
4 | Rob Van Dam | 2 | - | 21,106 | 32
5 | Kurt Angle | 3 | - | 37,74 | 22
6 | Chris Jericho | 3 | - | 31,108 | 8
7 | Kane | 4 | 13,14 | 61,124 | 20
8 | Booker T | 3 | - | 42,79 | 3
9 | Chris Benoit | 3 | - | 9,25 | 7
10 | Edge | 3 | - | 53,118 | 14
11 | Christian | 2 | - | 53,118 | 10
14 | Lita | 0 | - | 31,110 | 61
17 | William Regal | 3 | - | 51,121 | 44
18 | Carlito | 3 | 11,12 | 58,124 | 4
21 | Mr. McMahon | 2 | - | 0,0 | 77
22 | Eric Bischoff | 2 | - | 0,0 | 68
23 | Rey Mysterio | 1 | - | 4,10 | 30
24 | J. B. Layfield | 3 | 7,8 | 31,57 | 18
26 | Big Show | 4 | - | 8,101 | 1
27 | Trish Stratus | 0 | - | 53,118 | 65
28 | Tajiri | 2 | - | 50,124 | 41
29 | Eugene | 3 | - | 16,35 | 15
30 | Ric Flair | 2 | - | 32,60 | 31
31 | Stacy Keibler | 0 | - | 19,40 | 63
32 | Mark Jindrak | 3 | - | 31,107 | 23
33 | Rene Dupree | 3 | - | 56,124 | 29
34 | Chris Masters | 3 | - | 4,9 | 9
35 | Scotty 2 Hotty | 2 | - | 18,104 | 34
36 | Heidenreich | 4 | - | 17,36 | 16
37 | Chavo Guerrero | 2 | - | 42,83 | 6
39 | John Cena | 3 | 9,10 | 20,105 | 19
40 | The Hurricane | 2 | - | 32,61 | 17
41 | Christy Hemme | 0 | - | 4,123 | 59
42 | Torrie Wilson | 0 | - | 11,27 | 64
43 | Spike Dudley | 1 | - | 62,124 | 38
45 | Shawn Michaels | 2 | - | 42,81 | 35
46 | Doug Basham | 3 | - | 34,65 | 12
47 | Danny Basham | 3 | - | 34,65 | 11
48 | Joy Giovanni | 0 | - | 20,41 | 60
49 | Brian Hebner | 2 | - | 0,0 | 66
50 | Michael Cole | 2 | - | 0,0 | 73
51 | Earl Hebner | 2 | - | 0,0 | 67
52 | Jim Ross | 2 | - | 0,0 | 70
53 | Jerry Lawler | 2 | - | 0,0 | 69
54 | Jonathan Coachman | 2 | - | 0,0 | 71
55 | Tazz | 3 | - | 0,0 | 74
56 | Eddie Guerrero | 3 | - | 42,83 | 13
57 | Robert Conway | 3 | - | 53,119 | 33
58 | Sylvain Grenier | 3 | - | 53,119 | 40
59 | Michelle McCool | 0 | - | 8,102 | 62
60 | Batista | 4 | - | 60,23 | 0
61 | Randy Orton | 3 | - | 24,48 | 28
62 | Muhammad Hassan | 3 | - | 21,42 | 25
63 | Paul London | 2 | - | 42,82 | 27
64 | Orlando Jordan | 3 | - | 8,24 | 26
65 | British Bulldog | 3 | - | 51,120 | 47
66 | Andre The Giant | 5 | - | 56,122 | 45
67 | Snitsky | 4 | - | 37,112 | 37
69 | Mankind | 3 | - | 31,109 | 55
71 | Hollywood Hogan | 3 | - | 4,99 | 49
72 | Hogan 80's | 4 | - | 4,99 | 48
73 | Shelton Benjamin | 3 | - | 39,113 | 36
74 | Charlie Haas | 2 | - | 35,111 | 5
77 | Bret Hart | 3 | - | 53,117 | 46
78 | Hulk Hogan | 3 | - | 4,99 | 51
80 | Junkyard Dog | 3 | - | 32,60 | 54
81 | Ted DiBiase | 3 | - | 26,50 | 58
82 | Jake Roberts | 3 | - | 9,103 | 52
83 | Jimmy Hart | 1 | - | 41,114 | 53
85 | Steven Richards | 3 | - | 37,73 | 39
86 | Theodore Long | 1 | - | 0,0 | 75
87 | Khosrow Daivari | 1 | - | 22,44 | 21
88 | Tony Chimmel | 1 | - | 0,0 | 76
89 | Lilian Garcia | 0 | - | 0,0 | 72

## 3. Entrance event packages (EVT2/EVT5 ids 1000-1128; EVT=data KB, EVS=script KB; * = has pyro in PRTCLS PCEN)
id | EVT KB | EVS KB
---|---|---
1000 | 581 | 1
1001 | 471 | 2
1002 | 1925 | 29
1003 | 1500 | 3
1004 | 1536 | 5
1005 | 5080 | 153
1006 | 1719 | 2
1007 | 1318 | 29
1008 | 717 | 3
1009 | 2100 | 2
1010 | 855 | 3
1011 | 528 | 1
1012 | 1123 | 2
1013 | 1123 | 2
1014 | 1579 | 57
1015 | 1268 | 3
1016 | 2318 | 57
1017 | 463 | 3
1018 | 2094 | 1
1019 | 1475 | 38
1020 | 2737 | 38
1021 | 1961 | 38
1022 | 1921 | 1
1023 | 2232 | 4
1024 | 337 | 2
1025 | 3400 | 124
1026 | 3531 | 76
1027 | 1017 | 1
1028 | 803 | 2
1029 | 3173 | 2
1030 | 4172 | 105
1031 | 2440 | 86
1032 | 737 | 3
1033 | 2822 | 3
1034 | 1246 | 4
1035 | 3200 | 105
1036 | 3474 | 5
1037 | 2685 | 3
1038* | 1075 | 1
1039 | 2735 | 3
1040 | 2939 | 76
1041 | 3821 | 48
1042 | 1311 | 29
1043 | 2804 | 38
1044 | 3022 | 57
1045 | 1949 | 3
1046 | 2887 | 3
1047 | 2270 | 3
1048* | 1654 | 4
1049 | 3171 | 48
1050 | 1831 | 29
1051 | 1776 | 29
1052* | 2466 | 2
1053 | 2661 | 38
1054 | 1759 | 2
1055 | 1189 | 2
1056 | 3035 | 96
1057 | 1124 | 4
1058 | 2721 | 38
1059 | 2901 | 38
1060* | 498 | 2
1061 | 2126 | 48
1062* | 815 | 1
1063 | 2098 | 29
1064* | 1084 | 1
1065 | 1506 | 2
1066 | 3015 | 48
1067 | 860 | 2
1068 | 1115 | 2
1069 | 2432 | 38
1070 | 185 | 2
1071 | 185 | 2
1072* | 1728 | 3
1073* | 1104 | 2
1074 | 3236 | 76
1075 | 1011 | 1
1076 | 803 | 2
1077 | 2916 | 48
1078 | 1851 | 2
1079 | 966 | 1
1080 | 119 | 1
1081 | 1021 | 2
1082 | 2850 | 3
1083 | 721 | 2
1084 | 2586 | 57
1085 | 3120 | 86
1086* | 2025 | 4
1087* | 488 | 4
1088 | 4356 | 2
1089* | 1092 | 2
1090 | 2735 | 38
1091 | 1117 | 2
1092 | 2934 | 95
1093 | 2633 | 3
1094 | 1994 | 67
1095 | 1492 | 2
1096 | 4723 | 57
1097 | 3424 | 67
1098* | 920 | 4
1099* | 1691 | 5
1100 | 3873 | 62
1101 | 3413 | 47
1102 | 3347 | 41
1103* | 3191 | 57
1104 | 2806 | 69
1105 | 1162 | 1
1106 | 2696 | 103
1107 | 1948 | 3
1108 | 2166 | 27
1109 | 2748 | 48
1110 | 2409 | 42
1111 | 2128 | 3
1112 | 2034 | 3
1113 | 1053 | 38
1114 | 737 | 1
1115 | 2057 | 2
1116 | 424 | 1
1117* | 1364 | 3
1118 | 424 | 1
1119* | 1250 | 3
1120 | 639 | 3
1121 | 214 | 2
1122 | 107 | 1
1123 | 242 | 2
1124 | 728 | 2
1125 | 728 | 2
1126 | 94 | 0
1127 | 1 | 0
1128 | 1163 | 1
9999 | 0 | 0

Findings:
- 129 packages; scripts cluster at 1-5 KB (no scripted extras), 29/38/48/57/67/76/86/95/105/124 KB (multiples of ~9.5 KB = N stacked script modules). Only 16 ids have pyro/particle data (PCEN).
- Mapping roster idx -> package id is NOT 1000+idx (Kane idx7 -> 1007 has no PCEN, Angle idx5 -> 1005 is 5 MB). Mapping is resolved at runtime (resource kind table 0x36C220, '/CENT/%04x'). Needs runtime capture (see Next).
- Tiny packages (<250 KB: 1070,1071,1080,1121,1122,1123,1126,1127) are shared/default or non-wrestler entrances.
- Triple H vs Undertaker: only comparable once their package ids are captured.

## 4. Titantron movies (MOVIEP/NMVP000-004.AFS, 20 each = 100 slots)
Placeholder = 84 KB stub (no real video).
- NMVP000: 20/20 real (1.6-10 MB)
- NMVP001: 19 real, slot 19 placeholder
- NMVP002: 17 real, slots 18,19 placeholder (17=378K tiny)
- NMVP003: real only at 5,6,11,13,16,17,18; 13 placeholders
- NMVP004: real only at 1,2; 18 placeholders
=> 66 real / 34 placeholder slots. A wrestler whose Titantron id lands in NMVP003/004 placeholder slots plays a ~black/blank screen even on real hardware-correct code.

## 5. Music (BGM/*.AFS)
BGM0 7 tracks (3 x 230K + 4 x 2K = jingles/stubs); BGM1 17 (8.6-17.8 MB, menu/long); BGM2 40 (3.7-6.7 MB entrance themes); BGM3 27 (3.7-4.7 MB, slot 19 = 230K stub). ENVB/ENVH are same-count env variants (BGM1ENV* flat 6.3 MB). All real except BGM3[19] and BGM0[3..6].
Total 84 full tracks; roster music pair values go up to 0x7c (124) so they index a combined table incl. the 40 extra ENV/other entries.

## 6. Arenas (BG.PAC STG, 75 stage entries)
Sizes 0.5-1.1 MB, ids 00-25,30-46,48-51,55-57,60-68,70,73,76-78 (gaps = unused ids). Outliers: STG 23 = 204K (probably a stub/low-detail arena), 56 = 333K, 70 = 455K, 55/64/65 ~ 550K (small arenas e.g. backyard/cell). Special objects: KYAK CAGE CAGL CELL LADR LADU LADC CASK FFYF WPON BORD (cage, ladders, casket, etc.).

## 7. Entrance motions (M.PAC ENTM, 90 sets)
Named by body class: RMA0/RWA0 (Rock-class male/female, 165K), 1-4 {M,W}{A,B}{0,1} generic, AM/AW/TM/TW/BM/BW/CM/CW {A,B,C}{0,1,2}, MMA0/MWA0 (166K), CEFC/CELT. 90 sets are shared by body class (not one per wrestler); which set a wrestler uses is chosen via the class byte +0x77 and is unverified.

## Next
1. Add a CD/resource-load logger (sceCdRead by LSN -> file name) and an entrance trigger by wrestler ID, then capture HHH and Undertaker package ids and diff sizes/missing loads.
2. Check heap/arena ceiling during HHH (bigger package + alt pair slot 5,6).

## Extra findings
- Music second value 124 is shared by Kane, Carlito, Tajiri, Rene Dupree, Spike Dudley: a default/fallback theme id (likely license-free or generic). Staff (refs, announcers) have 0,0 = no entrance music.
- Triple H (music 6,100) and Undertaker (42,79) both have real music; Triple H is one of 6 wrestlers with an alt asset pair (5,6), Undertaker has none. That extra HHH asset pair is the best lead for why his entrance renders sparser (an extra prop/asset that may fail to load).
- Titantron: 34 of 100 movie slots are 84K placeholders; which slot each wrestler uses is not stored in this 160-byte record (resolved from the profile table in M.PAC CHAR), so the wrestler->movie map still needs the CHAR/PRO sub-block decode.

## Runtime capture 1 ([evtrd] logger, CD.cpp, ISS-128)
CD read file sector = DPAC entry sector + 8. CAW "Triple H" entrance loaded EVT.PAC EVNT 4002 (555 KB) + EVTS 4002 (83 KB script); PCEN 4002 (141 KB pyro) exists. So CAW entrances use the generic 4000-series events, NOT the per-wrestler 1000-series packages. Real roster entrances (1000-1128) still need a capture from Exhibition.

## Runtime capture 2 (user report + framebuffer)
User: both HHH and Undertaker entrances are missing parts of the models. Live framebuffer (frame ~30415, entrance) shows the wrestler as a torso+arms only (no head/lower body), arena textures garbled (palette/CLUT-like noise), upper area sheared streaks. Both wrestlers affected, so this is a shared render problem, not per-wrestler data (packages 4002/4003 are 555K/505K, similar). Prime suspects: entrance camera near-plane/guard-band clipping (VU1 CLIP) dropping body parts, or per-part draw packets dropped; texture garbage = CLUT/TEX0 upload for arena in entrance.

## Runtime capture 3 (gifcap 6000 packets during entrance)
22,225 draws; only 231 contain a (0,0) vertex and 2 saturated: GS stream is 99% clean. So the missing head/legs are NOT dropped/garbled at the GS bridge; they are never submitted (or submitted with zero area). Gate is upstream: game per-part visibility/LOD/cull, or VU1 culling output. Next: find character part-draw list and its visibility mask during entrance vs in-match.

## Runtime capture 4 (in-match, 2026-11-01 01:43)
Match reached (after ~3 min load) and plays (animations/strikes respond to input). Screen: limbs/torso pieces scattered and displaced, textures garbled (not wireframe). GS stream: 22,127 textured TSTRIP, 0 line prims. Conclusion: entrance "missing parts" and in-match "exploded" wrestlers share one cause = wrong bone/pose matrices (the open pose bug, see claude/pose-investigation-2026-10-31.md; key loader 0x20F940, motion record decode vs hctp_motion.py) so body parts land off-screen. Not entrance data. Texture garbage is a second, separate issue (arena/skin CLUTs).

## Lighting check vs PCSX2 (2026-11-01 ~09:30)
tools/pcsx2gs.py converts a PCSX2 .gs.zst dump to gifdump3 chunks; analysis_tmp/vstats.py prints per-texture-mode vertex colour stats.
PCSX2 Undertaker entrance (233908): T8 tfx0 mean RGBA (43,54,92,126); T8 tfx3 highlight (11,36,160,0); T4 tfx3 (10,30,151,0).
Ours (entrance capture): T8 tfx0 (54-62,68-79,92-130,128); T8 tfx3 (9,34,159,0); T4 tfx3 (9,31,155,1).
=> VU1 lighting output (vertex colours, highlight pass) matches PCSX2; lighting math is NOT the bug. Remaining skin/colour problem is texture/CLUT content or UV/geometry. PCSX2 HHH entrance (234041): tfx0 (70,58,57), highlight (10,16,9) - warm lights.
Texture sheet of our top 16 T8 textures (analysis_tmp/texsheet.py) showed several noisy/garbled textures (stable between dumps, so not streaming): needs per-texture comparison with PCSX2.
