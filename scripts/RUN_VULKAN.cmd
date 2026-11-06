@echo off
title SvR06 run - Vulkan (paraLLEl-GS) test
rem Vulkan GS test launcher. Logs: analysis\run_err.txt / run_out.txt; look for [gsvk] lines. Progress: run takes ~8 min to load the demo.
set "SVR06_GS=vulkan"
call "%~dp0RUN_LOGGED.cmd"
