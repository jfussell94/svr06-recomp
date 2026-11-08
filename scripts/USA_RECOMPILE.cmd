@echo off
title SvR06 USA full re-recompile
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0usa_build.ps1" -Regen
