# HLE targets (derived from ELF strings)
| Layer | Evidence | Plan |
|---|---|---|
| CRI ADX (ADXT 9.42, ADXF 7.30, AHX, SJ) | `ADXT/PS2EE Ver.9.42` | Replace ADXT_* with host decoder (vgmstream/libadx) -> miniaudio; enables custom entrance music (`audio_map.json`) |
| CRI Sofdec (SFD 1.946, MWSFD 3.33, M2V, MPS) | `CRI SFD/PS2EE`, `mwPlySfd*` | Replace mwPlySfd_* with FFmpeg playback (movies in MOVIEP/*.AFS) |
| CRI SVM 1.58 / SFX / SUD | strings | stub RPC server; map to host threads |
| libcdvd (sceCd*) | syscall stubs | map to host filesystem, serve from `assets/` via PACLISTP.H/ADDLISTP.H lists |
| eenet / inet / PPP / DNAS | MODULE/*.IRX | stub out entirely (offline) |
| SIF RPC / IOP modules | MODULE/*.IRX (padman, mcman, libsd...) | HLE padman->SDL3, mcman->host save dir, libsd->mixer |
| GS/GIF/VIF1/VU1 | 4.8k COP2 insns + DMA | GS->Vulkan/D3D12 renderer; VU1 microprograms extracted at runtime from VIF1 packets |
