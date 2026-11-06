# SVR06 file lookup table (generated 2026-10-28)

Purpose: quick reference of what every game file / code unit / port source file is. Roles marked (inferred) come from names, sizes and analysis docs, not from full decoding. Regenerate by re-running the generator note at the bottom.

## 1. Disc layout (PAL SLES-53676, folder = path on disc)

### (root)  (6 files, 2.6 MB)
Role: boot files: SLES_536.76 = main ELF (EE code, entry 0x100008); SYSTEM.CNF boot config; *.H = PAC list headers (PACLISTP.H/ADDLISTP.H/THMCAB.H); FILELIST-LSN.TXT = disc file/LSN list

| file | size |
|---|---|
| ADDLISTP.H | 1,651 |
| PACLISTP.H | 1,810 |
| SLES_536.76 | 2,729,064 |
| SYSTEM.CNF | 56 |
| THMCAB.H | 122 |
| WWE SMACKDOWN! VS. RAW 2006 (EUROPE, AUSTRALIA).ISO-FILELIST-LSN.TXT | 16,958 |

### BELT  (6 files, 17.3 MB)
Role: championship belt models/textures (BELT/CENTER/JEWEL/NAME/RELIEF/SIDE .PAC; per-belt id /BELT/%04x /CENT/ /RELF/ /SIDE/ /NAPL/ resource kinds, table at ELF 0x36C220)

| file | size |
|---|---|
| BELT.PAC | 1,886,208 |
| CENTER.PAC | 3,364,864 |
| JEWEL.PAC | 49,152 |
| NAME.PAC | 753,664 |
| RELIEF.PAC | 9,865,216 |
| SIDE.PAC | 2,246,656 |

### BGM  (18 files, 2088.4 MB)
Role: AFS sound archives: BGM<k>.AFS music (CRI ADX), PBPPAC.AFS, BG_ENV/SFX_ENV (ambience, sfx), CHEER/ANNOUNCE/CASVOICE (crowd, announcer, commentary), SSND1/2 (sound effects banks); opened through the CRI/IOP sound path (sids 0x300/0x301)

| file | size |
|---|---|
| ANNOUNCE.AFS | 70,518,784 |
| BGM0.AFS | 727,040 |
| BGM1.AFS | 220,024,832 |
| BGM1ENVB.AFS | 109,918,208 |
| BGM1ENVH.AFS | 109,926,400 |
| BGM2.AFS | 168,749,056 |
| BGM2ENVB.AFS | 130,754,560 |
| BGM2ENVH.AFS | 130,754,560 |
| BGM3.AFS | 104,269,824 |
| BGM3ENVB.AFS | 84,346,880 |
| BGM3ENVH.AFS | 84,361,216 |
| BG_ENV.AFS | 19,795,968 |
| CASVOICE.AFS | 931,840 |
| CHEER.AFS | 72,833,024 |
| PBPPAC.AFS | 308,133,888 |
| SFX_ENV.AFS | 3,104,768 |
| SSND1.AFS | 370,739,200 |
| SSND2.AFS | 199,913,472 |

### BIN  (1 files, 2.5 MB)
Role: BIN.PAC = packed overlays (DPAC->PAC->BPE->MWo3) decoded to 11 overlays (ovx<Name>)

| file | size |
|---|---|
| BIN.PAC | 2,648,064 |

### ED0  (8 files, 17.4 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED02.PAC | 827,392 |
| ED03.PAC | 3,422,208 |
| ED04.PAC | 964,608 |
| ED05.PAC | 11,827,200 |
| ED06.PAC | 755,712 |
| ED07.PAC | 126,976 |
| ED08.PAC | 241,664 |
| ED09.PAC | 126,976 |

### ED1  (10 files, 52.1 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED10.PAC | 503,808 |
| ED11.PAC | 696,320 |
| ED12.PAC | 985,088 |
| ED13.PAC | 1,982,464 |
| ED14.PAC | 3,403,776 |
| ED15.PAC | 16,384 |
| ED16.PAC | 27,539,456 |
| ED17.PAC | 4,628,480 |
| ED18.PAC | 9,115,648 |
| ED19.PAC | 5,765,120 |

### ED2  (10 files, 29.0 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED20.PAC | 1,288,192 |
| ED21.PAC | 1,423,360 |
| ED22.PAC | 2,433,024 |
| ED23.PAC | 16,384 |
| ED24.PAC | 4,528,128 |
| ED25.PAC | 4,677,632 |
| ED26.PAC | 16,384 |
| ED27.PAC | 16,021,504 |
| ED28.PAC | 16,384 |
| ED29.PAC | 16,384 |

### ED3  (10 files, 27.1 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED30.PAC | 13,422,592 |
| ED31.PAC | 2,041,856 |
| ED32.PAC | 2,308,096 |
| ED33.PAC | 4,513,792 |
| ED34.PAC | 16,384 |
| ED35.PAC | 886,784 |
| ED36.PAC | 2,129,920 |
| ED37.PAC | 309,248 |
| ED38.PAC | 1,302,528 |
| ED39.PAC | 1,470,464 |

### ED4  (10 files, 2.4 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED40.PAC | 290,816 |
| ED41.PAC | 280,576 |
| ED42.PAC | 256,000 |
| ED43.PAC | 16,384 |
| ED44.PAC | 157,696 |
| ED45.PAC | 192,512 |
| ED46.PAC | 247,808 |
| ED47.PAC | 542,720 |
| ED48.PAC | 538,624 |
| ED49.PAC | 16,384 |

### ED5  (10 files, 8.8 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED50.PAC | 47,104 |
| ED51.PAC | 999,424 |
| ED52.PAC | 507,904 |
| ED53.PAC | 4,003,840 |
| ED54.PAC | 802,816 |
| ED55.PAC | 419,840 |
| ED56.PAC | 309,248 |
| ED57.PAC | 28,672 |
| ED58.PAC | 323,584 |
| ED59.PAC | 1,763,328 |

### ED6  (4 files, 1.2 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ED60.PAC | 61,440 |
| ED61.PAC | 499,712 |
| ED62.PAC | 288,768 |
| ED63.PAC | 442,368 |

### LICENSE  (1 files, 0.0 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| LIBEENET.TXT | 12,689 |

### MODULE  (21 files, 1.0 MB)
Role: IOP IRX modules (libsd, sdrdrv, cdvd, mc, pad, USB/net...), loaded by the emulated IOP

| file | size |
|---|---|
| DEV9.IRX | 14,309 |
| DNAS300.IMG | 278,641 |
| EZMIDI.IRX | 11,609 |
| INET.IRX | 143,661 |
| INETCTL.IRX | 24,861 |
| LIBSD.IRX | 30,085 |
| MCMAN.IRX | 96,181 |
| MCSERV.IRX | 7,385 |
| MODHSYN.IRX | 63,037 |
| MODMIDI.IRX | 21,941 |
| NETCNF.IRX | 65,909 |
| NTGUI2.IRX | 31,029 |
| PADMAN.IRX | 45,925 |
| PPP.IRX | 102,589 |
| PPPOE.IRX | 17,305 |
| SDRDRV.IRX | 9,161 |
| SIO2MAN.IRX | 6,641 |
| SMAP.IRX | 17,089 |
| USBD.IRX | 34,993 |
| USBKB.IRX | 12,853 |
| USBPSPCM.IRX | 54,401 |

### MOVIEP  (8 files, 467.6 MB)
Role: movie AFS archives (SFD mpeg + ADX audio): LOGOP, OPP, ENDP(.AFS128 typo is in game data), NMV999; played by native player Kernel/Stubs/SvrMovie.cpp

| file | size |
|---|---|
| ENDP.AFS | 206,964,736 |
| LOGOP.AFS | 30,586,880 |
| NMVP000.AFS | 82,413,568 |
| NMVP001.AFS | 61,552,640 |
| NMVP002.AFS | 41,345,024 |
| NMVP003.AFS | 18,714,624 |
| NMVP004.AFS | 7,045,120 |
| OPP.AFS | 41,715,712 |

### NTGUI2  (1 files, 1.0 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| NTGUI2.ELF | 1,097,264 |

### NTGUI2/CNF  (2 files, 0.0 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ICON.SYS | 964 |
| SYS_NET.ICO | 33,688 |

### NTGUI2/NTCNFBIN  (9 files, 4.1 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| BG.RGB | 860,160 |
| BG2.RGB | 1,146,880 |
| BG_PAL.RGB | 983,040 |
| EFF.BD | 127,536 |
| EFF.HD | 560 |
| EUROPEAN.PF | 637,456 |
| JIS2UCS.BIN | 131,072 |
| TOP.TM2 | 263,232 |
| UCS2JIS.BIN | 131,072 |

### NTGUI2/RESOURCE/KEY/JP  (7 files, 0.2 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| ABC.BIN | 33,032 |
| AIUHIRA.BIN | 36,624 |
| AIUKANA.BIN | 37,168 |
| KIGO.BIN | 6,064 |
| KIGO_DUM.BIN | 62,392 |
| NUM.BIN | 16,632 |
| QWERTY.BIN | 31,296 |

### NTGUI2/RESOURCE/KEY/OTHER  (12 files, 0.3 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| CFRENCHQ.BIN | 29,768 |
| DUTCHQ.BIN | 29,808 |
| FRENCHQ.BIN | 29,728 |
| GERMANQ.BIN | 29,760 |
| IDIOMCHR.BIN | 32,696 |
| ITALIANQ.BIN | 29,728 |
| NUMF.BIN | 16,016 |
| PORTUGQ.BIN | 29,744 |
| SPANISHQ.BIN | 29,752 |
| SPSIGN.BIN | 33,432 |
| UKQ.BIN | 29,752 |
| USQ.BIN | 29,112 |

### NTGUI2/RESOURCE/TEX/HELP  (10 files, 0.5 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| SKBHDCH.TM2 | 32,896 |
| SKBHENG.TM2 | 32,896 |
| SKBHFCH.TM2 | 32,896 |
| SKBHGMN.TM2 | 32,896 |
| SKBHITA.TM2 | 32,896 |
| SKBHITEM.TM2 | 246,848 |
| SKBHJP.TM2 | 32,896 |
| SKBHKR.TM2 | 32,896 |
| SKBHPTG.TM2 | 32,896 |
| SKBHSP.TM2 | 32,896 |

### NTGUI2/RESOURCE/TEX/SKB  (6 files, 0.4 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| FEPBAR.TM2 | 17,472 |
| SKBBG.TM2 | 131,200 |
| SKBBTN.TM2 | 66,624 |
| SKBKEY.TM2 | 131,200 |
| SKBMDBAR.TM2 | 17,472 |
| TFIELD.TM2 | 8,320 |

### PAC  (27 files, 1224.6 MB)
Role: main resource archives: GAME2D, M (character/profile records, 432 B each; music id +0x13D, movie id +0x13C), MENU, MISC, SOUND, GM, EDIT, PRTCLS, MOVIE_P, EVT..EVT5, _AUTO, etc.

| file | size |
|---|---|
| BG.PAC | 60,678,144 |
| CH.PAC | 26,433,536 |
| CH2.PAC | 31,780,864 |
| CH3.PAC | 23,148,544 |
| COM.PAC | 710,656 |
| DUMMY.DAT | 629,145,600 |
| EDIT.PAC | 991,232 |
| EVT.PAC | 40,286,208 |
| EVT2.PAC | 128,022,528 |
| EVT3.PAC | 36,978,688 |
| EVT4.PAC | 9,809,920 |
| EVT5.PAC | 120,621,056 |
| GAME2D.PAC | 17,649,664 |
| GM.PAC | 3,293,184 |
| GM_MODE.PAC | 5,427,200 |
| LOCKER.PAC | 5,408,768 |
| M.PAC | 54,052,864 |
| MENU.PAC | 16,461,824 |
| MISC.PAC | 35,624,960 |
| MOVIE_P.PAC | 5,672,960 |
| PBPLIST.PAC | 135,168 |
| PRTCLS.PAC | 6,008,832 |
| SHOP.PAC | 3,334,144 |
| SOUND.PAC | 21,932,032 |
| _AUTO.PAC | 151,552 |
| _IRX300.PAC | 306,864 |
| _STARTUP.PAC | 23,236 |

### THMCAB  (7 files, 13.4 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| BELTT.PAC | 882,688 |
| CPAT.PAC | 4,962,304 |
| CPLT.PAC | 1,736,704 |
| JWLT.PAC | 169,984 |
| MTLT.PAC | 145,408 |
| NAMET.PAC | 1,306,624 |
| SIDET.PAC | 4,890,624 |

### THMCAS  (1 files, 28.4 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| THMCAS.PAC | 29,804,544 |

### THMLOC  (1 files, 10.3 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| LRTHL.PAC | 10,788,864 |

### mc0/BESLES-53676SD7  (3 files, 0.9 MB)
Role: (inferred) game data archives, see file names

| file | size |
|---|---|
| BESLES-53676SD7 | 901,288 |
| SDvsRAW.ico | 50,968 |
| icon.sys | 964 |


## 2. Code units of the game
| unit | where | what |
|---|---|---|
| main ELF | disc `SLES_536.76`, load 0x100000-0x30C4D0 code, data above; entry 0x100008 | EE game engine. 7,226 functions recompiled to `PS2Recomp/ps2xRuntime/src/runner/sub_XXXXXXXX_0x....cpp` (do not hand-edit; patches via `tools/codeaudit/patch_*.py`, backups `.bak_*`) |
| overlays (11) | `BIN/BIN.PAC`, banks `ovx<Name>` (CAS, Create, Event, Game, Locker, MainGame, Menu, Moves, OnlineMenu, Title) + `dnas` (online auth, not recompiled) | menu / match / create-a-superstar code loaded at runtime; recompiled by `tools/ovl_recomp.py` into `overrides/ovx*_*.cpp`; function counts: MainGame 1294, OnlineMenu 1560, Menu 1040, Create 911, Game 793, Event 389, CAS 358, Locker 332, Moves 256, Title 7 |
| IRX modules | `MODULE/` | IOP sound/cd/memory-card/pad drivers executed by the IOP emulator (`ps2xIOP`) |
| VU0/VU1 microcode | inside ELF/PAC data (VU0 programs at ELF 0x30C58C.., 10 MPG packets) | trig/matrix helpers (VU0), geometry/skinning (VU1); listing `analysis/codeaudit/vu0_programs.txt` (local only) |

## 3. Key addresses (main ELF)
| address | what |
|---|---|
| 0x1BEC50 | skeleton_init (bones, matrices) |
| 0x10C960 | euler_to_matrix |
| 0x1DFFE0 / 0x1E1E90 | motion-set initialiser / MotionBank ctor |
| 0x1C9DB0, 0x1CF8A0 | sound-stream base / derived ctor (ADX BGM) |
| 0x10D340/0x10D3B0/0x10D400 | collision: volume merge / overlap test / overlap-then-call |
| 0x4D7320 (overlay game) | match collision test between two objects |
| 0x25BB80 | per-wrestler resource key builder (table 0x36C220: /BELT /CENT /RELF /SIDE /NAPL) |
| 0x1A3F68 | IOP sound-slot status poll (sid 0x80000900) |
| 0x1C0980 | worker thread: async SIF RPC queue (sound driver sids 0x300/0x301) |
| 0x114F70 | per-vertex screen transform / clip classifier |
| 0x1F1D74.. | software alpha blend (PMULTH) ; 0x15AEB4 halfword matrix dot chain (PHMADH) |
| 0x35B338.. | game debug printf strings ("DVCI:") |
| 0x3129D0 | ADXF partition table (zero under HLE) |

## 4. Port source map (what each file does)
Root: `C:\Users\ajizz\Documents\Recomp\` (device) = `svr06-recomp\` (game-specific) + `PS2Recomp\` (recompiler + runtime).

### svr06-recomp (game-specific)
| path | what |
|---|---|
| overrides/svr06_overrides.cpp | hooks and overrides (movie hooks, ADX music hooks, scene poll, [canary], heap watches) copied into the runner on every build |
| overrides/svr06_overlay.cpp/.h | overlay bank loader (`svr06ovl::addBank/addFunc/find/addLabels`) |
| overrides/svr06_ovx*.cpp, ovx*_*.cpp | generated overlay banks and their functions |
| config/svr06_ps2recomp.toml | recompiler config (entry points, stubs, skips, patches) |
| config/hle_plan.md | HLE replacement plan |
| scripts/ | BUILD_ONLY.cmd/build_only.ps1, FAST_REBUILD_AND_RUN.cmd, RUN_LOGGED.cmd (archives previous logs as analysis\prev_*), run.cmd (env vars: SVR06_* options), progress_lib.ps1 |
| tools/ | analyze_elf.py, index_assets.py, ovl_recomp.py, gifdump*.py, check_clean.py, progress.py/json, codeaudit/ (r5900emu model, insn_difftest, disasm, patch_mmi_hilo/patch_vu0_flags/patch_vdiv, string_xrefs, xref_addr, vudis, ...) |
| analysis/ | logs (run_err/run_out/run_log, prev_*), shots/, codeaudit outputs, asset_index.json, dumps (local only) |
| docs/ | all findings; FILE_LOOKUP.md (this), CODEAUDIT, GAMELOGIC, PROGRESS, REMAINING, HANG_*, MEMMAP, ... |
| mc0/, mc1/ | host memory-card folders (saves) |

### PS2Recomp/ps2xRuntime (runtime)
| path | what |
|---|---|
| src/lib/ps2_runtime.cpp | runtime core: memory map, heap, game thread, window/audio init (SPU2 output stream), FPU rounding switch |
| src/lib/ps2_memory.cpp | EE memory + IO registers, timers (timer jitter) |
| src/lib/Kernel/EeScheduler.cpp, Syscalls/*.cpp | EE threads/semaphores/interrupts, syscalls (Thread, RPC, System, Dispatcher) |
| src/lib/Kernel/Stubs/*.cpp | HLE library stubs: CD.cpp, DMA.cpp, GS.cpp, SIF.cpp, MemoryCard.cpp, Pad.cpp, LibC.cpp, MPEG/IPU, SvrMovie.cpp (native movies), SvrMusic.cpp (native ADX music), Audio.cpp, VU.cpp/Vu0ExactGen.cpp (exact VU0 stubs) |
| src/lib/gs/*, ps2_vif1_interpreter.cpp, vu/ps2_vu1_*.cpp | software GS, VIF1, VU1 interpreter (exact + fast paths) |
| src/lib/ps2_iop_host.cpp | glue between runtime and IOP emulator |
| include/ps2_vu0_flags.h | VU0 MAC/STATUS flag helper (my addition) |
| src/runner/ | generated recompiled code (never clean) |

### PS2Recomp/ps2xIOP (IOP emulator)
| path | what |
|---|---|
| src/emulator/core/iop_cpu.cpp, iop_kernel.cpp, iop_memory.cpp | IOP R3000 core, kernel services, memory + hardware regs + DMA |
| src/emulator/core/iop_spu2.cpp, include/ps2x/iop/spu2.h | SPU2 model (voices, ADPCM, ADSR, ADMA, mixer) added 2026-10-28 |
| src/emulator/imports/*.cpp | IOP library imports (cdvd, intrman, ioman, loadcore, sysmem, timrman, vblank...) |
| src/emulator/services/iop_rpc.cpp, iop_module_loader.cpp | SIF RPC server dispatch and IRX loader |
| src/modules/libsd.cpp, mcserv.cpp, dbcman.cpp | HLE services (sid 0x80000701 libsd, memory card, dbc) |

### PS2Recomp/ps2xRecomp (recompiler)
| path | what |
|---|---|
| src/lib/control_flow_emitter.cpp | branch/jal emission (JAL fix, BLEZ/BGTZ S64 fix) |
| src/lib/mmi_translation_helpers.cpp | MMI translation (HI/LO halfword fix) |
| src/lib/*translation*.cpp | per-instruction-class translators (COP1/COP2/integer) |

## 5. Resource namespace (PAC virtual paths)
See GAMELOGIC.md (resource namespace section): /BELT /CENT /CHAR /EDIT /EMD /MOT /MOTP /MOV /SFX /STG /TM* etc.

## 7. Inside the archives (added 2026-10-28)
DPAC = Yuke's directory pack: header word 0 ("hdr[0]") is roughly the entry count (unverified), entries are 4-character group tags followed by data; the first tag after `DPAC` names the group and equals the virtual path prefix the game asks for (see section 5). Roles below are inferred from tags/names/sizes unless marked (known).

### 7.1 PAC groups (tag -> role)
| file | group tag(s) | hdr[0] | role |
|---|---|---|---|
| BELT/BELT.PAC | BELT | 396 | belt base models/textures (known: /BELT/%04x) |
| BELT/CENTER.PAC | CENT | 776 | belt centre plates (/CENT/%04x; NOT the entrance - entrance data is elsewhere) |
| BELT/JEWEL.PAC | JEWL | 12 | belt jewels (/JEWL/%04d) |
| BELT/NAME.PAC | NAPL | 328 | belt name plates (/NAPL/%04x) |
| BELT/RELIEF.PAC | RELF | 2216 | belt relief/emboss maps (/RELF/%04x) |
| BELT/SIDE.PAC | SIDE | 2184 | belt side plates (/SIDE/%04x) |
| BIN/BIN.PAC | OVRM | 96 | 11 BPE-compressed code overlays (MWo3) - see overlays.md |
| ED0..ED6/ED<nn>.PAC | ED<nn> | 32-708 | edit/CAS/title-edit data sets numbered 02-63 (face parts, textures, Create-a-Superstar assets; known only by name) |
| PAC/BG.PAC | many | 1020 | backgrounds / arena scenery |
| PAC/CH.PAC, CH2.PAC, CH3.PAC | EMD (EMD2, EMD3) | 280/380/268 | wrestler character model sets (/EMD/%d, /EMD2/%d, /EMD3/%d) |
| PAC/COM.PAC | COM7 | 912 | common data (shared tables) |
| PAC/EDIT.PAC | EDIT | 64 | edit-mode common assets (/EDIT/FACE MOT PRTS BLOD) |
| PAC/EVT.PAC .. EVT5.PAC | EVNT, EVT2..EVT5 | 1536/1008/6208/688/1120 | event/entrance/cutscene data (EVT3 and EVT4 are POF0-heavy = motion/pose files) |
| PAC/GAME2D.PAC | GA2D | 536 | 2D game UI/HUD (/GA2D/BASE) |
| PAC/GM.PAC | GMEV | 1056 | game-mode event assets |
| PAC/GM_MODE.PAC | GMMD | 1344 | game-mode menu assets (MTP2 thumbnails) |
| PAC/LOCKER.PAC | LORO | 1608 | locker room scenes |
| PAC/M.PAC | BP6T MT0B GT24 | 1112 | wrestler profile/attribute records (432 B each, movie id +0x13C, music id +0x13D) and move data |
| PAC/MENU.PAC | MENU | 1816 | front-end menu art |
| PAC/MISC.PAC | NIII HHHH ... | 2640 | misc (/MISC/%d, /MISC/GPRM); mostly small records |
| PAC/MOVIE_P.PAC | - | 168 | movie metadata/titles |
| PAC/PBPLIST.PAC | PBP2 | 16 | play-by-play (commentary) lookup lists (/PBP2/DATA) |
| PAC/PRTCLS.PAC | PCEN | 952 | particle/effect data |
| PAC/SHOP.PAC | - | 2224 | shop/unlockables |
| PAC/SOUND.PAC | IECS | 336 | sound-effect banks for the IOP sound driver (SPU2 samples, VAG/ADPCM) - prime source of punches/hits/ring SFX (inferred) |
| PAC/_AUTO.PAC | AUTO MSG2 | 40 | auto-load data (/AUTO/SDB FLAG FNT2), font + messages |
| PAC/DUMMY.DAT | - | - | 600 MB filler used to place data on the disc (not read) |
| PAC/_IRX300.PAC, _STARTUP.PAC | - | - | IOP modules package + startup data |
| THMCAB/*.PAC | TMBM TMRP TMCP TMJW TMMT TMNP TMSP | 36-1940 | thumbnails (MTP2 textures) for belts: base, relief, centre plate, jewel, metal, name plate, side plate |
| THMCAS/THMCAS.PAC | MTP2 | 12036 | thumbnails for Create-a-Superstar parts |
| THMLOC/LRTHL.PAC | LRCT | 4224 | locker-room thumbnails |

### 7.2 AFS archives (CRI ADX/AHX audio and Sofdec video)
| file | entries | contents |
|---|---|---|
| BGM/BGM0.AFS | 7 | BGM000-006.adx (menu/system music, small) |
| BGM/BGM1.AFS, BGM2.AFS, BGM3.AFS | 17 / 40 / 27 | BGM040-067... licensed entrance/menu songs (ADX, 10-12 MB each) |
| BGM/BGM1ENVB/ENVH.AFS, BGM2ENV*, BGM3ENV* | 17 / 40 / 27 each | same songs with an alternate mix ("ENV" = in-arena filtered version; B/H = two variants) |
| BGM/BG_ENV.AFS | 8 | Base01-08.adx arena background ambience |
| BGM/SFX_ENV.AFS | 28 | base9000 + 9001-9026.adx ambience/effect loops |
| BGM/CHEER.AFS | 152 | CC001-CC152.adx crowd cheer/boo/chant loops |
| BGM/ANNOUNCE.AFS | 1372 | ring announcer lines (LGxxx, TCxxx names; ADX) |
| BGM/CASVOICE.AFS | 5 | Create-a-Superstar voice clips cas90-94 |
| BGM/PBPPAC.AFS | 4728 | play-by-play commentary clips named by id (e.g. 10030010.adx) |
| BGM/SSND1.AFS, SSND2.AFS | 4000 / 2450 | streamed sound slots (most entries are 67-byte dummy24mono.ahx placeholders; real entries are ADX such as SDOpen1_1_00.adx, G4_1_00.adx) |
| MOVIEP/LOGOP.AFS | 5 | sd7logos_pal.sfd (publisher logos), sd7title.sfd (title movie), dummies |
| MOVIEP/OPP.AFS | 3 | sd7_opn_smack_PAL.sfd, sd7_opn_raw_PAL.sfd (opening) |
| MOVIEP/NMVP000-004.AFS | 20 each | nmv000-nmv019.sfd Titantron / entrance videos (MOV M000..M019 in /MOV/) |
| MOVIEP/ENDP.AFS | 1 | ending movie (the game asks for ENDP.AFS128, a typo in game data) |

### 7.3 IOP modules (MODULE/) and how the port handles them
| module | role | port |
|---|---|---|
| LIBSD.IRX / SDRDRV.IRX | sound library / driver (SPU2 access) | run on the IOP emulator; SPU2 hardware model in ps2xIOP iop_spu2.cpp |
| _IRX300.PAC (EE side package) | game sound driver (RPC sids 0x300/0x301) | runs on IOP emulator |
| MCMAN/MCSERV.IRX | memory card | HLE (MemoryCard.cpp, mcserv.cpp) |
| PADMAN/SIO2MAN.IRX | controller | HLE (Pad.cpp) |
| EZMIDI/MODMIDI/MODHSYN.IRX | MIDI/synth (unused by the game, sdk leftovers) | loaded, not used |
| INET/INETCTL/NETCNF/PPP/PPPOE/SMAP/DEV9.IRX, DNAS300.IMG | online play (network, DNAS) | not emulated (offline) |
| USBD/USBKB/USBPSPCM.IRX | USB keyboard etc. | not used |
| NTGUI2.IRX + NTGUI2/ | network setup GUI (separate ELF NTGUI2.ELF) | not used |

### 7.4 Naming conventions seen
- `*_PAL` = region variant; `ENV` = arena-filtered music; `dummy*` = placeholder entries (67-byte ahx / 241 KB sfd) kept to preserve entry indices.
- Wrestler resources are keyed by hex id `%04x`; stages by `/STG/%02d`; movies `/MOV/M000-M019`.
- Memory card save `mc0/BESLES-53676SD7` holds the game save + icon (SDvsRAW.ico, icon.sys).

## 6. Regeneration
Section 1 is generated from `analysis/asset_index.json` (tools/index_assets.py output); sections 2-5 are hand-written; update when files move.
