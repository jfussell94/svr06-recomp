@echo off
title SvR06 run with GS dump window at vsync tick 12000 (press Start at the title around 2:00-2:30)
set SVR06_GSDUMPTICK=12000
set SVR06_GSDUMPLEN=6
call "%~dp0RUN_LOGGED.cmd"
