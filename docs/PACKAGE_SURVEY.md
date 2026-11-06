# SvR06 (PAL) package survey (structure only, no game data)

- PAC files are `DPAC` containers: u32 magic, u32 entry count, u32 total size, u32 version(7); a directory at 0x800 of 8-byte records
  (4-char tag, u16 first-entry, u16 count). Tags nest (group tag followed by its children).
- `PAC\DUMMY.DAT` (600 MB) is entirely zero bytes: pure disc padding, nothing hidden.
- MENU.PAC: two complete menu sets (`MENU` and `MNPL`), each with FNT2 (font, 342 entries), MNLB (menu labels), CHSL/CHAS (character select),
  SF2A/B/D (menu 2D), FISE/FISJ, STGW/STTX (stage), SLCR, ONL/ONAN (online), DNAS, MOT2 (motion), SHOP, KETX/KEYB (keyboard), CREN/CETX, PVTX, CRDT (credits).
  MACH group = per-event backgrounds/logos (SMCK, RAWA, VELO, HEAT, SURV, ARMG, RYRB, VENG, WMXX, NYRV, GRAM, TABO, WMIX, NWAY, ECWA, BALA). The PAL build should use the MNPL set: check which one the game loads.
- GAME2D.PAC: BASE, ITLY (Italian), TIPS/TIPI (31 loading-tip images each, 1066 entries per tip).
- MISC.PAC: DBGF (debug font), TEST (426 entries), SDB, MING (minigames), NET/IRX/NETD/VOIC (online modules), EVTM/EMOT, ET2D 0000-0019 plus 8888/9999, BSCL, NLDA.
- LOCKER.PAC: locker room scenes (WL, FR, LT, CN, AD, LC, TR, WO, CR groups).
- _AUTO.PAC: AUTO group with FNT2 (89), AANM, FLAG, SDB (boot-time font and flags).
- COM.PAC: COM7 AI data, one 21-entry record per character.
- Manual (PDF) lists main menu: Exhibition, Game Modes, Create Modes, Locker Room, Online, Options; GM mode: Schedule, E-mail, Rosters, Weekly Reports, Financials.

## PCSX2 reference flow (user snapshots, Documents\PCSX2\snaps, 2026-10-22 19:38-19:46; local only)
1. NOW LOADING (WWE logo) -> memory card dialog: metallic pipe frame, CAUTION icon, grey outlined centred text
   ("Checking. Do not remove Memory Card (8MB) (for PlayStation 2) in MEMORY CARD slot 1, reset, or switch off the console.")
   -> "[MEMORY CARD slot 1] Now Loading!" -> ATTENTION "Load Successful."
2. NOW LOADING -> legal text -> Sofdec/ADX logos? (ordering per snaps: THQ/JAKKS, Yukes, Sofdec, ADX, WWE Entertainment) -> opening videos
3. Title (logo video + "Press START button" bar) -> NOW LOADING (SvR logo) -> Main menu: Exhibition, Game Modes, Create Modes, Locker Room, Online, Options
   with a now-playing banner bottom-left ("You Don't Want War / Bumpy Knuckles - SmackDown! vs RAW Exclusive Produced by Hidden Agenda").
4. Exhibition sub-menu: Single, Tag Team, Triple Threat, Fatal 4-Way, Handicap, Royal Rumble, Main Events, Title Match; controller-assign screens; button hints "Select / Back".
