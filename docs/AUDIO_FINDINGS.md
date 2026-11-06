# Audio findings (SPU2 model) - 2026-10-28

## Model
- New SPU2 emulation: ps2xIOP/src/emulator/core/iop_spu2.cpp (2 cores x 24 voices, 2 MB SPU RAM, PSX ADPCM, ADSR, AutoDMA reader, mixer) routed from iop_memory.cpp (0x1F900000-0x1FA00000, DMA channels 4/7). Output: raylib AudioStream 48 kHz stereo in ps2_runtime.cpp. Env: SVR06_SPU2_OFF=1 (no stream), SVR06_SPU2_WAV=<file> (raw dump; scripts/run.cmd now defaults it to analysis\spu2_dump.raw; tools/spu2_wav.py makes a WAV + loudness), SVR06_SPU2_STATX=<hex> (override STATX read), SVR06_SPU2_ENDX_ZERO=1 (ENDX reads 0).
- Register offsets confirmed from the game's own driver: KOFF 0x1A4/0x1A6 (all-voices-off written at init), CORE_ATTR 0x19A (0x8000 enable), TSA 0x1A8/0x1AA, DATA(PIO) 0x1AC, ADMAS 0x1B0, ENDX 0x340/0x342, STATX 0x344, master volume block 0x760 (core0) / 0x788 (core1). The driver uploads a 16-byte silence block (flags 0x07 = end|loop|start) at SPU address 0x2800 halfwords (byte 0x5000) with PIO, then KONs all 24 voices (pitch 0x3FFF, volume 0).

## Black screen with first model (FIXED 2026-10-28)
- Symptom: window opens, black, frame loop 0; the IOP sound-driver thread (entry 0x49288) never registers its RPC servers, so the EE never gets past the sound bind.
- Cause: the driver polls STATX (0x344) and ENDX (0x340/0x342) during init. The first model returned STATX=0x80 constantly and live ENDX (voices finish the silence block immediately). The old plain register map returned 0 until the first DMA, then 0x80.
- Fix: STATX now = stored value, bit 7 set when a DMA/ADMA starts (legacy behaviour); ENDX reads 0 by default (SVR06_SPU2_ENDX_LIVE=1 gives live bits; SVR06_SPU2_STATX=<hex> overrides STATX for experiments). Verified: boots to NOW LOADING, frame loop alive, sound RPCs complete.
- Sound RPCs seen: sid 0x300 fn 3 (bind/init), sid 0x301 fn 0x6c/0x6a/0x67 (setup), fn 0x69 with 2368-byte payloads (bank/sample data chunks while NOW LOADING). Play commands are expected after the load finishes.

## Output status (2026-10-28 05:50)
- After the demo load finished, the 560 s WAV dump (analysis\spu2_dump.raw -> tools\spu2_wav.py) contains audio: peak 18302, windows of activity at ~225 s and later, all from the core0 ADMA ring (IOP 0x57E10/0x58610, 2048 bytes, ringpeak 18304). The waveform is smooth but flat-topped at exactly +-18302 (36k samples at -18302, 35k at +18301 in 4 s), so it is hard-clipped by the game's IOP-side mixer or by a wrong gain/duplication in my path. Needs listening and a check of the ring data against a clean reference.
- No `[spu2] KON` of a real sample (ssa != 0x2800) has been logged yet, so ADPCM voice playback (SFX/commentary/crowd) is still untested; only the ADMA stream path produced sound.
- State logger shows master volume 0x3FFF both cores, BVOL(core0)=0x7FFF, AVOL(core1)=0x7FFF, CORE_ATTR c080/c081.
- Next: run through the title/menu/match and log the first real KON; compare volumes; consider routing core0 through core1 (AVOL) instead of summing.

## In-match audio finding (2026-10-28 15:25 UTC, singles match from Exhibition)
- During a live singles match: 0 `[spu2] KON` of any sample, 135 of 137 core0 ring reports silent (ring peak only 18304 twice), voices=0, sound RPC fn 0x69 packets keep flowing (`[sndrpc] #90`). So no SFX/commentary/crowd is produced at all.
- Hypothesis: ENDX is forced to 0 (needed to get past driver init), so the IOP sound driver believes all 24 voices it KON'd with the init silence block are still playing and never finds a free voice. Test: `scripts\RUN_SPU2_ENDXLIVE.cmd` (live ENDX with the corrected STATX). If init stalls again, make ENDX live only after the first fn 0x69 packet.

## Voice/crowd audio archives decoded (2026-11-02, ISS-137)
All of the game's speech, crowd and ambience audio is **CRI ADX** in AFS archives under `BGM/` - the same format as the
music the native player already streams, so no new decoder is needed. AFS layout: `"AFS\0"`, u32 count,
count x (u32 offset, u32 size), then (u32 metaOffset, u32 metaSize); the metadata block holds one 48-byte record per
entry whose first 32 bytes are the clip's own file name. That name table is what makes clip ids usable.

| Archive | Entries | Size | Clip names | Content |
|---|---|---|---|---|
| `BGM/PBPPAC.AFS` | 4728 | 293 MB | `10030010.adx` (8 digits) | play-by-play commentary, 24 kHz mono. Leading digit = commentator: 1 (1177 clips), 2 (1160), 3 (1213), 4 (1178) - i.e. two 2-man teams. Digits 2-5 = line group (biggest: 0950 x198, 0960 x152, 0401 x134, 0402 x125, 0400 x124, 0970 x124), last digits = variant |
| `BGM/ANNOUNCE.AFS` | 1372 | 67 MB | `LG001.adx`, `TC001.adx` | ring announcer, 24 kHz mono: **686 LG = Lilian Garcia** + **686 TC = Tony Chimel**, the same lines per announcer. Some have suffixes (`LG004_a`, `TCJYD`, `TC_OnTheWay_Accom`) |
| `BGM/CHEER.AFS` | 152 | 69 MB | `CC001.adx`-`CC152.adx` | crowd beds, ~450 KB each (long loops) |
| `BGM/BG_ENV.AFS` | 8 | 18 MB | `Base01.adx`-`Base08.adx` | arena ambience, 24 kHz **stereo** |
| `BGM/SFX_ENV.AFS` | 28 | 2 MB | `base9000.adx`, `9001.adx`+ | ambience SFX |
| `BGM/SSND1.AFS` | 4000 | 353 MB | `SDOpen1_1_00.adx` + `dummy24mono.ahx` | SFX bank; nearly all entries are 67-byte `dummy24mono.ahx` placeholders in this build |
| `BGM/SSND2.AFS` | 2450 | 190 MB | `G4_1_00.adx` + dummies | second SFX bank, same placeholder pattern |
| `BGM/CASVOICE.AFS` | 5 | 0.9 MB | `cas90.adx`-`cas94.adx` | Create-a-Superstar voices |
| `BGM/BGM*.AFS` | 7-40 | 0.7-209 MB | `BGM040.adx`+ | music, 44.1 kHz stereo (already played natively) |

Lookup lists: `PAC/PBPLIST.PAC` (PBP2, 16 entries, `/PBP2/DATA`) holds the play-by-play id lists the game picks lines from.

Why none of it plays today: the IOP sound driver never reaches voice playback (no `[spu2] KON` of a real sample), and the
EE-side CRI path cannot address the archives because the ADXF partition table at `0x3129D0` is all zero under HLE
(same root cause as ISS-010 for movies). The remedy follows the music/movie pattern: play the ADX natively.

## The EE -> IOP sound request block decoded (2026-11-02, ISS-137)
`[snd69d]` (RPC.cpp, logs only the words that change) during the title/menu shows SIF RPC **sid 0x301 fn 0x69** is the
game's "play a sound" channel - one call per sound, a 2368-byte block, double-buffered:

| Offset | Meaning | Evidence |
|---|---|---|
| `+0x1c` | command buffer pointer, **ping-pongs** between two addresses (`0x203d69c0` / `0x203d7300`) every call | alternates perfectly call to call |
| `+0x28` | id/index, wide range (`0x1c,0x21,0x22,0x27,0x2f,0x50,0x60`) | changes per sound |
| `+0x2c` | small set (`0x24,0x25,0x26,0x3c`) - looks like bank/category or sound type | only 4 values seen at the menu |
| `+0x30` | **pitch**, clustered around `0x1000` (`0xe22`-`0x10a0`) - SPU2 pitch where `0x1000` = 1.0 | classic per-shot pitch randomisation |
| `+0x38` | **volume**, two identical u16 (L/R), around `0x2000` of `0x3fff` max | `221a221a`, `21312131`, ... |
| `+0x44` | **sequence counter**, +1 per call | 1,2,3,...,0xc |
| `+0x8`, `+0x24`, `+0x920` | set to 1 once at init | only in call #1 |

So the EE asks for sounds here, not through the CRI layer. Menu sounds already flow through it (12 requests just from
entering the menu). What is still unknown is which field carries the **clip id** for streamed speech: commentary is
300 MB of ADX and cannot be resident in the 2 MB SPU RAM, so the IOP must stream it from disc on request. Next step:
capture `[snd69d]` during a real match and correlate the ids that appear when the ring announcer speaks and when a
commentary line plays; then drive `svr06_voice_play` from those fields instead of the (non-functional) IOP driver.

## PAC/SOUND.PAC holds the sound lookup tables (2026-11-02, ISS-137)
`SOUND.PAC` is a DPAC with **data base `0x4000`** (entry sector * 2048 + 0x4000 = file offset; the sector numbers alone
point at padding, which is why a naive read gives all zeros). Two groups:

- **ADX** (7): `REFM` 148736 (referee male), `REFF` 133632 (referee female), `SPRF` 282368, `RR` 29696,
  `SFXK` 1516800, **`CHER` 1280** (crowd lookup, at `0x208800`), **`ANNO` 2304** (ring announcer lookup, at `0x209000`).
- **SFX** (33): `LIST` 28928 (a nested `PAC ` of 15 offset/size entries) then the SPU banks `B1MN`, `B1NO`, `B2NO`,
  `B1CB`.. `B2SC`, 19.8 MB in all - these are what the IOP uploads into SPU RAM.

**`ANNO` = the ring-announcer table.** Records of 5 u16 (10 bytes) `[id, nameClip, 109, 112, flag]`, id running 0,1,2,...
for the first ~89 records (then the layout changes/pads). The clip numbers index **ANNOUNCE.AFS** directly and all fall
in the LG (Lilian Garcia) half, 0..685 - so Tony Chimel is almost certainly the same index **+686** (`TC001` is entry
686). Durations fit the roles: idx 109 = `LG094` 1.95 s and idx 112 = `LG097` 1.45 s are the fixed connector phrases used
by 89 of the records, while the per-record `nameClip` (e.g. idx 131 = `LG110`, 4.06 s) is the long per-wrestler line.
So announcing a wrestler = play `nameClip`, 109 and 112 in order, +686 for the SmackDown announcer.

`CHER` (1280 B at `0x208800`) is the matching crowd-reaction table, u16 records indexing CHEER.AFS.

Still compressed/unknown: `PAC/PBPLIST.PAC`, whose `DATA` blob is a nested `PAC ` -> **`BPE `** block (the same
compression as the `BIN.PAC` overlays). It holds the per-situation play-by-play id lists. Either a BPE decompressor is
needed, or the decompressed table can be lifted from RAM once a match is running.
