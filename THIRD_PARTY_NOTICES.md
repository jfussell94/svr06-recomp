# Third-party notices

This project builds on the following open-source software. Each component keeps its own license; the license files are included with the component in this repository (or its upstream source).

| Component | License | Use |
|---|---|---|
| PS2Recomp / ps2xRuntime (ran-j) | GNU GPL v3 | Static recompiler and runtime |
| paraLLEl-GS | GNU LGPL v3 | Vulkan Graphics Synthesizer renderer |
| Granite | MIT | Vulkan backend of paraLLEl-GS |
| raylib | zlib | Window, input, audio output |
| Dear ImGui / rlImGui | MIT | Debug overlay |
| xxHash | BSD-2-Clause | Hashing for texture-pack matching |
| Ghidra, Ghidra Emotion Engine Reloaded | Apache 2.0 | Analysis tools only, not shipped |
| Python, PyInstaller | PSF; GPL with bootloader exception | Launcher |
| PCSX2 (IPU/MPEG sources) | GPL-3.0+ (IPU_MultiISA, mpeg2_vlc: GPL-2.0+, from mpeg2dec by Michel Lespinasse and Aaron Holtzman) | `SvrIpu.cpp` is generated from PCSX2's IPU code; PCSX2 is also the behaviour reference for the GS and IOP |
| FFmpeg (optional, `PS2X_ENABLE_FFMPEG`) | LGPL-2.1+/GPL depending on the build | Movie and MPEG decoding |
| Montserrat | SIL Open Font License 1.1 | In-game settings menu font |
| hctp-recomp, Def Jam Vendetta recomp, Urban Reign recomp, WWE Crush Hour PC | GPL-3.0 / Apache-2.0 (varies) | Design references only; ideas and bug leads were reviewed, no code copied |

Because PS2Recomp is GPL v3, this project as a whole is distributed under GPL v3 (see LICENSE) unless the maintainer replaces the GPL parts.

**No game data is included.** WWE, SmackDown! vs. Raw, THQ, Yuke's and related names and assets belong to their owners; this is an unofficial fan project with no affiliation.
