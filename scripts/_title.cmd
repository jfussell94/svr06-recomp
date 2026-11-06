@echo off
title SvR06 title capture (frames every 4 s to analysis\title_shots, SPU2 audio to analysis\title_spu2.raw)
set "SVR06_GS=vulkan"
set "SVR06_SHOTDIR=%~dp0..\analysis\title_shots"
set "SVR06_SPU2_WAV=%~dp0..\analysis\title_spu2.raw"
call "%~dp0RUN_LOGGED.cmd"
