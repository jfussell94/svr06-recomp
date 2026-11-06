# Movie / video findings

Written 2026-10-27. Status: working (logo, brand video, title movie play; natural end works; Start skips; title holds last frame until Start).

## 1. Format
- The PAL disc uses CRI Sofdec MPEG-2 program streams (`.sfd`, pack header `00 00 01 BA`; `mpeg1video` video + `adpcm_adx` audio), NOT Bink (Bink is the Xbox 360 games). Other PS2 SvR/HCTP titles use `.IPU` movies and Sony ADS; not this disc.
- Files are AFS entries under `MOVIEP\`.

## 2. Movie map (verified from AFS headers)
| Archive | Entries |
|---|---|
| `LOGOP.AFS` | 0 `sd7logos_pal.sfd` (9.3 MB), 1-2 dummy, 3 `sd7title.sfd` (19 MB), 4 missing |
| `OPP.AFS` | 0 dummy, 1 `sd7_opn_smack_PAL.sfd`, 2 `sd7_opn_raw_PAL.sfd` |
| `ENDP.AFS` | `sd7ending_PAL.sfd` (the `ENDP.AFS128` typo is in game data, harmless) |
| `NMVP000..004.AFS` | `nmvNNN.sfd` Titantrons (86 KB entries are placeholders) |

The game indexes movies by "partition id": ptid 10-14 = NMVP000-004, 15 = LOGOP, 16 = OPP, 17 = ENDP. Intro order: ptid 15 idx 0 (logo) -> ptid 16 idx 2 (brand video) -> ptid 15 idx 3 (title movie, must hold the last frame until Start).

## 3. Design of the native player
- `Kernel/Stubs/SvrMovie.cpp`: FFmpeg demux/decode, frames drawn by `svr06_movie_present` in the raylib loop. API: `svr06_movie_start/stop/done/hold/skip_requested`.
- Hooks in `overrides/svr06_overrides.cpp`:
  - `movieNameHook` @ `0x1498B8` captures the request (ptid=a0, idx=a1); log `[movie] request ptid=15 idx=0`. Call chain: `0x182E60` -> `0x1833E8` -> `0x183458` -> `0x1498B8`.
  - `movieSkipHook` @ `0x19BCC0` (per-frame movie-object update) starts playback when the object is in state 3 and sets PLAYEND on natural finish or on a user skip (Enter/Space/pad Start). The title movie holds its last frame (`svr06_movie_hold`).
  - `adxGetTimeHook` @ `0x18A188` returns real elapsed ms for the movie position clock (written to `*a1=ms, *a2=1`), except for the title movie (`s_isTitle ? 0 : now`).
- Movie object `F3A8C0`: `+0x48/+0x4C` state (1 prepare, 3 playing, 4 end, 6 stopped/ending); per-movie attrs at `obj+0xA04+idx*4` via `19B4F8(obj,idx)`; counters at `obj+0xD28+...`. End detector `198CA8` -> `198D18/198D88/198E50/198EA8`; `198EA8` uses `19C0E0` (position num/den from `[obj+0xD28+0x28C/0x290]` via `18A188`) and `19C618` = `172658`; compare `num*1000 >= attr36*den` (attr36 = 0x22291E0 us, about 35.9 s). Probes `[mvobj]` and `[mend]` print state transitions.

## 4. libmpeg / IPU
- In-game MPEG decode uses libmpeg with the IPU (`0x121xxx` area; the range `0x123000-0x127000` is intentionally hand-modified and left out of automatic entry-point additions). The IPU decode path is a port derived from PCSX2 (GPLv3): attribution must be added to the project's notices before publishing.
- Fixed bug: libmpeg's wait loop after FDEC (`0x121328`, `ld IPU_CMD; bgezl`) hung because branches compared 32 bits (ISS-005).

## 5. Disproved ideas
- Poking INT_MAX into `F3B880/F3B888` to end the movie.
- Using `ADXT_GetTime` `0x154800`.
- `198EA8`'s `a1` is `obj+0x950` (an earlier delay-slot confusion read it as `0xFF000000`).

## 6. Open items
- Movie hitch: the last video before the brand video hitches on a black screen; pressing Start skips past it (input leakage or a prepare/timing issue; log line `stuck-prepare ... -> PLAYEND` is related) - not investigated.
- Titantrons during matches (NMVP) not verified (match freeze, ISS-100).
- Importing Titantrons from other SVR games is a planned opt-in feature.
