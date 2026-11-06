# PS2_PROJECT_STATE (WWE SvR 2006 PAL, SLES_536.76)
Adopted workflow from hkmodd/ps2-recomp-Agent-SKILL (reference only):
- Never clean-build; never edit generated out_*.cpp; fixes go in runtime layer, TOML, overrides/svr06_overrides.cpp.
- Circuit breaker: after 3 identical failures stop, consult PCSX2 / docs, change approach.
- Prefer runtime register probing (probe hooks) over guessing; keep test runs short; avoid listing huge runner dir.
- Record lessons here at session end.
## Status
- ~96% title flow / ~58% playable.
- Open: degenerate fade/clear quads (0x114B50 corners equal) -> stacked caution text; font table/atlas; movies/IPU; audio; input.
- [quad0]/[quad] hook added to covThunk; awaiting cycle results (first cycle had no hits).
## Lessons
- Runs end at ~45s, before tick 3000 ([xfersum] needs more).
- [stated by user] SvR games use Yuke's engine and Bink (.bik) video. CHECKED: this PAL SvR06 disc's MOVIEP/*.AFS entries (LOGOP, ENDP, NMVP*) contain MPEG program streams (00 00 01 BA pack headers) i.e. PSS/Sofdec-style MPEG-2, not Bink, so IPU/MPEG2 decode applies here. movieSkipHook at 0x19BCC0 stays until then.
- Fade/clear quad bug: object 0x644EB0 (vtbl 0x37CD00) draws with size 0 (+0xa0/+0xa4 never set) -> all corners equal; stack fade objects get sizes correctly via setSize (0x114560, alias inside sub_114550). Resumable functions: mid-function entries alias to owner with ctx->pc. Direct jal calls bypass covThunk hooks.
- User's media tool: C:\Users\ajizz\Desktop\New folder\SVR\Program (reference only, do not copy game data). Format facts from it: SvR06 PS2 titantrons = MOVIEP/NMVP00x.AFS entries nmvNNN.sfd (CRI Sofdec = MPEG-2 program stream; .bik is the X360 games); music = BGM/BGM<k>.AFS CRI ADX (bgm<i>.adx); other PS2 SvR/HCTP use .IPU movies and Sony ADS; CHAR/PRO records in PAC/M.PAC (432 B, movie +0x13C, music +0x13D). AFS reader: 'AFS\0', count, (offset,size)* , optional name table (ptr,len) with 48-byte name records. Tool files of interest: afs_reader.py, Mod Maker Tools/ps2_media.py, discfs.py, ps2_char.py, jboy.py (models).
- RESOLVED red herring: zero-size quad 0x644EB0 is layout item '001' of ATB0 layout whose record (w,h)=(0,0) by data; per-frame apply 0x115FE0 calls setSize(item+0,item+4). Degenerate quads are expected, not the garble cause.
- NEW lead: PCSX2 per-frame header = libgs sceGsDBuffDc packet (22 A+D: FRAME fbp 0/0x50 alternating, XYOFFSET y 0x7800/0x7808 field offset, SCISSOR 640x256, clear sprite TEST 0x30000). Recomp GS stream has none; libgs is HLE-stubbed via toml stubs (sceGs*@0x11xxxx) though recompiled originals exist (sub_0011D840 etc.). Plan: restore originals at runtime (replaceFunction) so real packets flow through DMA/GIF.
- MOVIE MAP (verified from assets AFS headers, user hint to check Program tool): LOGOP.AFS = [0 sd7logos_pal.sfd 9.3MB, 1-2 dummy, 3 sd7title.sfd 19MB, 4 missing]; OPP.AFS = [0 dummy, 1 sd7_opn_smack_PAL.sfd, 2 sd7_opn_raw_PAL.sfd]; ENDP.AFS = [sd7ending_PAL.sfd]; NMVP000-004.AFS = nmvNNN.sfd titantrons (86KB = placeholder). All MPEG-PS (00 00 01 BA). Assets "ISO" is an extracted folder under assets\.
- MOVIE BLOCKER: game prints "E211151: ADXF_GetFnameRangeEx() faild." (fn 0x183458 -> 0x1498b8); ADXF partition ptinfo table at 0x3129d0 (ptid*4) is all zero => partitions never loaded (IOP-side ADXF read). EE sceCdRead shows no movie-LBN reads. "ENDP.AFS128" typo is in game data, harmless. Added [cdrd] (EE) and [iopcd] (IOP iop_cdvd.cpp readSectors) logs.

## Native movie player (13:34, confirmed working by user)
- SvrMovie.cpp (ps2xRuntime/src/lib/Kernel/Stubs): FFmpeg decodes .sfd (mpeg1video + adpcm_adx) from AFS entries under MOVIEP/; drawn by svr06_movie_present in the raylib loop.
- svr06_overrides.cpp: movieNameHook on 0x1498B8 captures (ptid=a0, idx=a1; ptid 10-17 = NMVP000-004, LOGOP, OPP, ENDP); movieSkipHook (0x19BCC0) starts playback in state 3 and sets PLAYEND on finish or Enter/Space.
- Verified: ptid=15 idx=0 -> LOGOP entry 0 plays. Next: confirm OPP/ENDP/NMVP, then music (ADX) via native decode.
