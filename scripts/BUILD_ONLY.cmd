@echo off
title SvR06 build only (no launch)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_only.ps1"
