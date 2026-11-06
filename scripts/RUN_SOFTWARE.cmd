@echo off
title SvR06 run - software GS (developer reference)
rem Developer reference run: the software GS renderer instead of Vulkan. Logs: analysis\run_err.txt / run_out.txt ([vram] / [disp] lines). Game runs in this window's child; it ends when the game window is closed.
echo [SvR06] software GS reference run started %TIME% - close the game window to finish.
set "SVR06_GS=software"
call "%~dp0RUN_LOGGED.cmd"
echo [SvR06] run finished %TIME%
